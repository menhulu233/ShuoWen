import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:window_manager/window_manager.dart';

import '../core/engine_client.dart';
import '../core/format.dart';
import '../core/l10n.dart';
import '../core/theme.dart';
import '../widgets/common.dart';
import 'onboarding.dart';

class SettingsWindow extends StatefulWidget {
  const SettingsWindow({super.key, required this.client});

  final EngineClient client;

  @override
  State<SettingsWindow> createState() => _SettingsWindowState();
}

class _SettingsWindowState extends State<SettingsWindow> with WindowListener {
  int _tab = 0;
  bool _recording = false;
  int _hotkeySerial = 0;
  bool _checkingMic = false;
  bool _micChecked = false;
  List<Map<String, dynamic>> _devices = [];
  final TextEditingController _testController = TextEditingController();

  EngineClient get client => widget.client;
  Map<String, dynamic> get s => client.settings;

  @override
  void initState() {
    super.initState();
    windowManager.addListener(this);
    windowManager.setPreventClose(true);
    _hotkeySerial = client.hotkeySerial;
    client.addListener(_onClient);
    client.events.listen(_onEvent);
    _loadDevices();
  }

  @override
  void dispose() {
    windowManager.removeListener(this);
    client.removeListener(_onClient);
    _testController.dispose();
    super.dispose();
  }

  @override
  void onWindowClose() {
    // Settings is opened on demand. Exiting the renderer instead of hiding it
    // reclaims the whole Flutter process; the engine relaunches it on demand.
    exit(0);
  }

  void _onEvent(Map<String, dynamic> event) {
    if (event['event'] == 'shutdown') {
      exit(0);
    }
    if (event['event'] == 'focus' &&
        (event['data'] as Map?)?['window'] == 'settings') {
      windowManager.show();
      windowManager.focus();
    }
  }

  void _onClient() {
    if (client.hotkeySerial != _hotkeySerial) {
      _hotkeySerial = client.hotkeySerial;
      if (_recording && mounted) {
        setState(() => _recording = false);
        final data = client.hotkeyCaptured;
        if (data != null) {
          _snack('已设置：${data['name'] ?? ''}');
        }
      }
    }
  }

  void _snack(String message) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(content: Text(message), duration: const Duration(seconds: 2)),
    );
  }

  Future<void> _patch(Map<String, dynamic> patch) async {
    try {
      await client.patchSettings(patch);
    } catch (error) {
      _snack(error.toString());
    }
  }

  Future<void> _loadDevices() async {
    setState(() => _checkingMic = true);
    try {
      final devices = await client.micDevices();
      if (!mounted) return;
      setState(() {
        _devices = devices;
        _micChecked = true;
        _checkingMic = false;
      });
    } catch (_) {
      if (!mounted) return;
      setState(() {
        _micChecked = false;
        _checkingMic = false;
      });
    }
  }

  bool _bool(String key, bool fallback) => s[key] as bool? ?? fallback;
  String _str(String key, String fallback) => s[key] as String? ?? fallback;
  double _double(String key, double fallback) =>
      (s[key] as num?)?.toDouble() ?? fallback;

  @override
  Widget build(BuildContext context) {
    final l10n = L10nScope.of(context);
    final welcomed = s['Welcomed'] == true;
    return Scaffold(
      backgroundColor: SwColors.bg,
      body: Column(
        children: [
          SwTitleBar(
            title: '说文 ShuoWen',
            subtitle: '${l10n.t('settings.title')} · ${_statusText(l10n)}',
            trailing: _statusPill(l10n),
          ),
          SwConnectionBanner(client: client),
          Expanded(
            child: Row(
              children: [
                _sidebar(l10n),
                Expanded(
                  child: Container(
                    color: SwColors.bg,
                    child: welcomed
                        ? _tabContent(l10n)
                        : OnboardingView(client: client),
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  String _statusText(L10n l10n) {
    if (client.engineStatus == 'ready') {
      return client.listening ? l10n.t('hud.listening') : l10n.t('common.ready');
    }
    if (client.engineStatus == 'error') return l10n.t('common.error');
    return l10n.t('common.loading');
  }

  Widget _statusPill(L10n l10n) {
    final color = statusColor(client.engineStatus, client.listening);
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.10),
        borderRadius: BorderRadius.circular(999),
        border: Border.all(color: color.withValues(alpha: 0.25)),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          SwStatusDot(color: color, pulse: client.listening),
          const SizedBox(width: 6),
          Text(
            _statusText(l10n),
            style: TextStyle(
              color: color,
              fontSize: 11.5,
              fontWeight: FontWeight.w600,
            ),
          ),
        ],
      ),
    );
  }

  Widget _sidebar(L10n l10n) {
    final items = <(int, IconData, String)>[
      (0, Icons.tune, l10n.t('nav.general')),
      (1, Icons.keyboard_voice_outlined, l10n.t('nav.dictation')),
      (2, Icons.memory, l10n.t('nav.model')),
      (3, Icons.history, l10n.t('nav.records')),
      (4, Icons.verified_user_outlined, l10n.t('nav.permissions')),
      (5, Icons.info_outline, l10n.t('nav.about')),
    ];
    return Container(
      width: 214,
      decoration: const BoxDecoration(
        color: SwColors.surface,
        border: Border(right: BorderSide(color: SwColors.hairline)),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(18, 20, 18, 16),
            child: Row(
              children: [
                const SwBrandMark(size: 34),
                const SizedBox(width: 10),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        l10n.t('app.name'),
                        style: const TextStyle(
                          color: SwColors.text,
                          fontSize: 15,
                          fontWeight: FontWeight.w700,
                        ),
                      ),
                      Text(
                        l10n.t('app.tagline'),
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: const TextStyle(
                          color: SwColors.textMuted,
                          fontSize: 10,
                        ),
                      ),
                    ],
                  ),
                ),
              ],
            ),
          ),
          const Padding(
            padding: EdgeInsets.symmetric(horizontal: 16),
            child: Divider(height: 1),
          ),
          const SizedBox(height: 8),
          for (final item in items)
            _navItem(
              icon: item.$2,
              label: item.$3,
              selected: _tab == item.$1,
              onTap: () => setState(() => _tab = item.$1),
            ),
          const Spacer(),
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 0, 16, 16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    SwStatusDot(
                      color: statusColor(client.engineStatus, client.listening),
                      pulse: client.listening,
                    ),
                    const SizedBox(width: 7),
                    Expanded(
                      child: Text(
                        client.engineStatus == 'ready'
                            ? '${l10n.t('common.ready')} · ${client.hotkeyName}'
                            : _statusText(l10n),
                        style: const TextStyle(
                          color: SwColors.textMuted,
                          fontSize: 10.5,
                        ),
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 6),
                Text(
                  'v0.1.0 · ${client.engine['engineVersion'] ?? ''}',
                  style: const TextStyle(
                    color: SwColors.textMuted,
                    fontSize: 9.5,
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _navItem({
    required IconData icon,
    required String label,
    required bool selected,
    required VoidCallback onTap,
  }) {
    return Padding(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 2),
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(9),
        child: AnimatedContainer(
          duration: const Duration(milliseconds: 150),
          padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
          decoration: BoxDecoration(
            color: selected ? SwColors.accentSoft : Colors.transparent,
            borderRadius: BorderRadius.circular(9),
          ),
          child: Row(
            children: [
              Icon(
                icon,
                size: 17,
                color: selected ? SwColors.accentA : SwColors.textMuted,
              ),
              const SizedBox(width: 10),
              Text(
                label,
                style: TextStyle(
                  color: selected ? SwColors.text : SwColors.textMuted,
                  fontSize: 12.5,
                  fontWeight: selected ? FontWeight.w600 : FontWeight.w500,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _tabContent(L10n l10n) {
    final page = switch (_tab) {
      0 => _general(l10n),
      1 => _dictation(l10n),
      2 => _model(l10n),
      3 => _records(l10n),
      4 => _permissions(l10n),
      _ => _about(l10n),
    };
    return SingleChildScrollView(
      padding: const EdgeInsets.fromLTRB(30, 26, 30, 34),
      child: Align(
        alignment: Alignment.topCenter,
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 900),
          child: page,
        ),
      ),
    );
  }

  Widget _general(L10n l10n) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        SwPageHeader(
          title: l10n.t('nav.general'),
          subtitle: l10n.t('app.tagline'),
        ),
        const SizedBox(height: 22),
        SwCard(
          title: '启动与显示',
          child: Column(
            children: [
              SwToggleRow(
                label: l10n.t('general.launch'),
                help: l10n.t('general.launch.help'),
                value: _bool('LaunchAtLogin', true),
                onChanged: (value) => _patch({'LaunchAtLogin': value}),
              ),
              const Divider(),
              SwSettingRow(
                label: l10n.t('general.language'),
                help: l10n.t('general.language.help'),
                trailing: _dropdown(
                  value: _str('Language', 'auto'),
                  items: const {
                    'auto': '自动 / Auto',
                    'zh': '简体中文',
                    'en': 'English',
                    'ja': '日本語',
                    'ko': '한국어',
                  },
                  onChanged: (value) {
                    if (value != null) _patch({'Language': value});
                  },
                ),
              ),
              const Divider(),
              SwSettingRow(
                label: l10n.t('general.tray'),
                help: l10n.t('general.tray.help'),
                trailing: SwPill(
                  text: l10n.t('common.on'),
                  color: SwColors.success,
                ),
              ),
            ],
          ),
        ),
        const SizedBox(height: 14),
        SwCard(
          title: '输出',
          child: Column(
            children: [
              SwToggleRow(
                label: l10n.t('general.clipboard'),
                help: l10n.t('general.clipboard.help'),
                value: _bool('ClipboardOverwrite', false),
                onChanged: (value) => _patch({'ClipboardOverwrite': value}),
              ),
              const Divider(),
              SwToggleRow(
                label: l10n.t('general.traditional'),
                help: l10n.t('general.traditional.help'),
                value: _bool('OutputTraditional', false),
                onChanged: (value) => _patch({'OutputTraditional': value}),
              ),
            ],
          ),
        ),
        const SizedBox(height: 14),
        const _PrivacyNote(),
      ],
    );
  }

  Widget _dictation(L10n l10n) {
    final mode = _str('Mode', 'Paste');
    final trigger = _str('Trigger', 'Hold');
    final hudStay = _double('HudStaySeconds', 0.5).toString();
    final micDevices = <String, String>{'': '系统默认 / System default'};
    for (final device in _devices) {
      final id = device['id']?.toString() ?? '';
      if (id.isNotEmpty) {
        micDevices[id] = device['name']?.toString() ?? id;
      }
    }
    final selectedMic = _str('MicDeviceId', '');
    if (selectedMic.isNotEmpty && !micDevices.containsKey(selectedMic)) {
      micDevices[selectedMic] = '当前设备';
    }

    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        SwPageHeader(
          title: l10n.t('nav.dictation'),
          subtitle: l10n.t('dictation.mode.paste.desc'),
        ),
        const SizedBox(height: 22),
        SwCard(
          title: l10n.t('dictation.mode'),
          child: SwSegmented<String>(
            showDescriptions: true,
            value: mode,
            onChanged: (value) => _patch({'Mode': value}),
            options: [
              SwSegmentedOption(
                value: 'Paste',
                label: l10n.t('dictation.mode.paste'),
                description: l10n.t('dictation.mode.paste.desc'),
              ),
              SwSegmentedOption(
                value: 'Type',
                label: l10n.t('dictation.mode.type'),
                description: l10n.t('dictation.mode.type.desc'),
              ),
              SwSegmentedOption(
                value: 'OnCall',
                label: l10n.t('dictation.mode.oncall'),
                description: l10n.t('dictation.mode.oncall.desc'),
              ),
            ],
          ),
        ),
        const SizedBox(height: 14),
        SwCard(
          title: l10n.t('dictation.trigger'),
          child: Column(
            children: [
              SwSegmented<String>(
                value: trigger,
                onChanged: (value) => _patch({'Trigger': value}),
                options: [
                  SwSegmentedOption(
                      value: 'Hold', label: l10n.t('dictation.trigger.hold')),
                  SwSegmentedOption(
                      value: 'Latch', label: l10n.t('dictation.trigger.latch')),
                  SwSegmentedOption(
                      value: 'Toggle', label: l10n.t('dictation.trigger.toggle')),
                ],
              ),
              const SizedBox(height: 16),
              SwSettingRow(
                label: l10n.t('dictation.hotkey'),
                help: l10n.t('dictation.hotkey.help'),
                trailing: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Container(
                      padding: const EdgeInsets.symmetric(
                          horizontal: 12, vertical: 9),
                      decoration: BoxDecoration(
                        color: _recording
                            ? SwColors.accentSoft
                            : Theme.of(context).colorScheme.surfaceContainerHighest,
                        borderRadius: BorderRadius.circular(9),
                        border: Border.all(
                          color: _recording
                              ? SwColors.accentA
                              : SwColors.hairline,
                        ),
                      ),
                      child: Text(
                        _recording
                            ? l10n.t('dictation.hotkey.recording')
                            : hotkeyLabel(
                                (s['HotkeyVk'] as num?)?.toInt() ?? 0xA3,
                                (s['HotkeyMods'] as num?)?.toInt() ?? 0,
                                l10n.t,
                              ),
                        style: TextStyle(
                          color: _recording
                              ? SwColors.accentB
                              : SwColors.text,
                          fontSize: 12,
                          fontFamily: 'Cascadia Mono',
                          fontWeight: FontWeight.w600,
                        ),
                      ),
                    ),
                    const SizedBox(width: 8),
                    IconButton(
                      tooltip: l10n.t('dictation.hotkey'),
                      onPressed: () async {
                        setState(() => _recording = true);
                        await client.captureHotkey();
                      },
                      icon: const Icon(Icons.keyboard, size: 18),
                    ),
                  ],
                ),
              ),
            ],
          ),
        ),
        const SizedBox(height: 14),
        SwCard(
          title: l10n.t('dictation.cue'),
          child: Column(
            children: [
              SwToggleRow(
                label: l10n.t('dictation.cue'),
                help: l10n.t('dictation.cue.help'),
                value: _bool('CueEnabled', true),
                onChanged: (value) => _patch({'CueEnabled': value}),
              ),
              const Divider(),
              SwSettingRow(
                label: l10n.t('dictation.cueTheme'),
                trailing: _dropdown(
                  value: _str('CueTheme', 'chime'),
                  items: const {
                    'chime': '清脆 Chime',
                    'tick': '轻点 Tick',
                    'soft': '柔和 Soft',
                    'drop': '水滴 Drop',
                    'marimba': '木琴 Marimba',
                  },
                  onChanged: (value) {
                    if (value != null) _patch({'CueTheme': value});
                  },
                ),
              ),
              const Divider(),
              SwSettingRow(
                label: l10n.t('dictation.cueVolume'),
                trailing: _dropdown(
                  value: _str('CueVolume', 'low'),
                  items: const {'low': '低', 'med': '中', 'high': '高'},
                  onChanged: (value) {
                    if (value != null) _patch({'CueVolume': value});
                  },
                ),
              ),
              const Divider(),
              SwSettingRow(
                label: l10n.t('dictation.hudStay'),
                help: l10n.t('dictation.hudStay.help'),
                trailing: _dropdown(
                  value: hudStay,
                  items: const {
                    '0.0': '立刻',
                    '0.5': '0.5 秒',
                    '1.0': '1 秒',
                    '2.0': '2 秒',
                    '4.0': '4 秒',
                  },
                  onChanged: (value) {
                    if (value != null) {
                      _patch({'HudStaySeconds': double.parse(value)});
                    }
                  },
                ),
              ),
            ],
          ),
        ),
        const SizedBox(height: 14),
        SwCard(
          title: l10n.t('dictation.mic'),
          child: Column(
            children: [
              SwSettingRow(
                label: l10n.t('dictation.mic'),
                help: l10n.t('dictation.mic.help'),
                trailing: SizedBox(
                  width: 250,
                  child: _dropdown(
                    width: 250,
                    value: selectedMic,
                    items: micDevices,
                    onChanged: (value) {
                      if (value != null) _patch({'MicDeviceId': value});
                    },
                  ),
                ),
              ),
              const Divider(),
              SwToggleRow(
                label: l10n.t('dictation.mute'),
                help: l10n.t('dictation.mute.help'),
                value: _bool('MicMuted', false),
                onChanged: (value) => _patch({'MicMuted': value}),
              ),
              const Divider(),
              SwToggleRow(
                label: l10n.t('dictation.history'),
                help: l10n.t('dictation.history.help'),
                value: _bool('HistoryEnabled', true),
                onChanged: (value) => _patch({'HistoryEnabled': value}),
              ),
              const SizedBox(height: 12),
              Row(
                children: [
                  ValueListenableBuilder<double>(
                    valueListenable: client.levelNotifier,
                    builder: (context, level, _) => SwMeter(
                      level: client.micMuted ? 0 : level,
                      bars: 18,
                      height: 24,
                    ),
                  ),
                  const SizedBox(width: 14),
                  Expanded(
                    child: Text(
                      (client.engine['micName'] as String?) ?? '',
                      maxLines: 1,
                      overflow: TextOverflow.ellipsis,
                      style: Theme.of(context).textTheme.bodySmall,
                    ),
                  ),
                ],
              ),
            ],
          ),
        ),
      ],
    );
  }

  Widget _model(L10n l10n) {
    final models = client.models;
    final tiers = (models['tiers'] as List?) ?? const [];
    final vad = (models['vad'] as Map?) ?? const {};
    final tierDescription = <int, String>{
      160: l10n.t('model.tier.160'),
      480: l10n.t('model.tier.480'),
      960: l10n.t('model.tier.960'),
      1920: l10n.t('model.tier.1920'),
    };
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        SwPageHeader(
          title: l10n.t('model.title'),
          subtitle: l10n.t('model.subtitle'),
        ),
        const SizedBox(height: 22),
        SwCard(
          title: l10n.t('model.engine'),
          trailing: TextButton.icon(
            onPressed: client.reloadEngine,
            icon: const Icon(Icons.refresh, size: 15),
            label: Text(l10n.t('model.engine.reload')),
          ),
          child: Column(
            children: [
              Row(
                children: [
                  SwStatusDot(
                    color: statusColor(client.engineStatus, client.listening),
                    pulse: client.listening,
                  ),
                  const SizedBox(width: 8),
                  Text(
                    _statusText(l10n),
                    style: const TextStyle(
                      color: SwColors.text,
                      fontSize: 13,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                  const Spacer(),
                  SwPill(
                    text: (client.engine['tier'] as String?) ?? 'Ms960',
                    color: SwColors.accentA,
                  ),
                  const SizedBox(width: 6),
                  SwPill(
                    text: ((client.engine['vad'] as String?) ?? 'energy')
                        .toUpperCase(),
                    color: SwColors.accentB,
                  ),
                ],
              ),
              if ((client.engine['error'] as String?)?.isNotEmpty == true) ...[
                const SizedBox(height: 10),
                Text(
                  client.engine['error'] as String,
                  style: const TextStyle(color: SwColors.error, fontSize: 12),
                ),
              ],
              const SizedBox(height: 12),
              Row(
                children: [
                  Text(
                    '${l10n.t('model.engine.version')}: '
                    '${client.engine['engineVersion'] ?? '—'}',
                    style: Theme.of(context).textTheme.bodySmall,
                  ),
                  const Spacer(),
                  Text(
                    l10n.t('model.bundledNote'),
                    style: Theme.of(context).textTheme.bodySmall,
                  ),
                ],
              ),
            ],
          ),
        ),
        const SizedBox(height: 14),
        SwCard(
          title: l10n.t('model.tier'),
          subtitle: l10n.t('model.tier.help'),
          child: Column(
            children: [
              for (final raw in tiers)
                if (raw is Map)
                  _tierCard(
                    l10n,
                    Map<String, dynamic>.from(raw),
                    tierDescription,
                  ),
            ],
          ),
        ),
        const SizedBox(height: 14),
        SwCard(
          title: l10n.t('model.vad'),
          subtitle: l10n.t('model.vad.help'),
          child: Column(
            children: [
              Row(
                children: [
                  Icon(
                    vad['ready'] == true
                        ? Icons.check_circle
                        : Icons.warning_amber_rounded,
                    size: 17,
                    color: vad['ready'] == true
                        ? SwColors.success
                        : SwColors.warn,
                  ),
                  const SizedBox(width: 8),
                  Expanded(
                    child: Text(
                      vad['ready'] == true
                          ? 'Silero VAD 已就绪'
                          : '当前使用能量 VAD（Silero 未下载）',
                      style: Theme.of(context).textTheme.bodyMedium,
                    ),
                  ),
                  if (vad['downloading'] == true)
                    TextButton(
                      onPressed: () => client.cancelDownload(vad: true),
                      child: Text(l10n.t('common.cancel')),
                    )
                  else if (vad['ready'] != true)
                    TextButton(
                      onPressed: client.downloadVad,
                      child: Text(l10n.t('model.vad.download')),
                    ),
                ],
              ),
              if (vad['downloading'] == true) ...[
                const SizedBox(height: 10),
                SwProgressLine(
                  value: (vad['total'] as num?) != null &&
                          (vad['total'] as num).toInt() > 0
                      ? (vad['received'] as num).toInt() /
                          (vad['total'] as num).toInt()
                      : 0,
                ),
              ],
            ],
          ),
        ),
        const SizedBox(height: 14),
        SwCard(
          title: l10n.t('model.source'),
          child: SwSettingRow(
            label: l10n.t('model.source'),
            help: l10n.t('model.source.help'),
            trailing: _dropdown(
              value: (models['source'] as String?) ?? 'official',
              items: const {
                'official': 'CDN 加速线路',
                'modelscope': 'ModelScope',
                'huggingface': 'HuggingFace',
              },
              onChanged: (value) {
                if (value != null) _patch({'ModelSource': value});
              },
            ),
          ),
        ),
      ],
    );
  }

  Widget _tierCard(
    L10n l10n,
    Map<String, dynamic> item,
    Map<int, String> descriptions,
  ) {
    final ms = (item['tier'] as num?)?.toInt() ?? 960;
    final active = item['active'] == true;
    final ready = item['ready'] == true;
    final bundled = item['bundled'] == true;
    final state = item['state']?.toString() ?? 'missing';
    final downloading = state == 'downloading';
    final received = (item['received'] as num?)?.toInt() ?? 0;
    final total = (item['total'] as num?)?.toInt() ?? 0;
    final error = item['error']?.toString() ?? '';
    return Padding(
      padding: const EdgeInsets.only(bottom: 9),
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 160),
        padding: const EdgeInsets.symmetric(horizontal: 13, vertical: 11),
        decoration: BoxDecoration(
          color: active
              ? SwColors.accentA.withValues(alpha: 0.08)
              : Theme.of(context).colorScheme.surfaceContainerHighest,
          borderRadius: BorderRadius.circular(11),
          border: Border.all(
            color: active
                ? SwColors.accentA.withValues(alpha: 0.35)
                : Colors.transparent,
          ),
        ),
        child: Column(
          children: [
            Row(
              children: [
                Text(
                  '$ms ms',
                  style: const TextStyle(
                    color: SwColors.text,
                    fontSize: 13,
                    fontWeight: FontWeight.w700,
                    fontFamily: 'Cascadia Mono',
                  ),
                ),
                const SizedBox(width: 10),
                Expanded(
                  child: Text(
                    descriptions[ms] ?? '',
                    style: Theme.of(context).textTheme.bodySmall,
                  ),
                ),
                if (active)
                  SwPill(
                    text: l10n.t('common.active'),
                    color: SwColors.accentA,
                  )
                else if (bundled)
                  SwPill(
                    text: l10n.t('common.bundled'),
                    color: SwColors.accentB,
                  )
                else if (ready)
                  SwPill(
                    text: l10n.t('common.downloaded'),
                    color: SwColors.success,
                  )
                else
                  SwPill(
                    text: l10n.t('common.missing'),
                    color: SwColors.textMuted,
                  ),
                const SizedBox(width: 8),
                if (downloading)
                  TextButton(
                    onPressed: () => client.cancelDownload(tier: ms),
                    child: Text(l10n.t('common.cancel')),
                  )
                else if (!active && ready)
                  TextButton(
                    onPressed: () => client.useTier(ms),
                    child: Text(l10n.t('common.use')),
                  )
                else if (!active && !ready)
                  FilledButton(
                    onPressed: () => client.downloadTier(ms),
                    child: Text(l10n.t('common.download')),
                  ),
                if (ready && !bundled && !active)
                  IconButton(
                    tooltip: l10n.t('common.delete'),
                    onPressed: () => _deleteTier(ms),
                    icon: const Icon(Icons.delete_outline, size: 17),
                  ),
              ],
            ),
            if (downloading) ...[
              const SizedBox(height: 8),
              SwProgressLine(
                value: total > 0 ? received / total : 0,
              ),
              const SizedBox(height: 5),
              Align(
                alignment: Alignment.centerRight,
                child: Text(
                  total > 0
                      ? '${formatProgress(received, total)} · ${item['file'] ?? ''}'
                      : (item['file']?.toString() ?? ''),
                  style: Theme.of(context).textTheme.bodySmall,
                ),
              ),
            ],
            if (state == 'error' && error.isNotEmpty) ...[
              const SizedBox(height: 6),
              Align(
                alignment: Alignment.centerLeft,
                child: Text(
                  error,
                  style: const TextStyle(color: SwColors.error, fontSize: 11),
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }

  Future<void> _deleteTier(int ms) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        backgroundColor: SwColors.surface,
        title: const Text('删除模型'),
        content: Text('删除本地下载的 $ms ms 模型？'),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: const Text('取消'),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            child: const Text('删除'),
          ),
        ],
      ),
    );
    if (ok == true) await client.deleteTier(ms);
  }

  Widget _records(L10n l10n) {
    final minutes = estimatedMinutes(client.lifetimeChars);
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        SwPageHeader(
          title: l10n.t('nav.records'),
          subtitle: l10n.t('records.privacy'),
        ),
        const SizedBox(height: 22),
        Container(
          width: double.infinity,
          padding: const EdgeInsets.all(22),
          decoration: BoxDecoration(
            gradient: SwTheme.accentGradient,
            borderRadius: BorderRadius.circular(16),
          ),
          child: Row(
            children: [
              Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    formatCount(client.lifetimeChars),
                    style: const TextStyle(
                      color: Colors.white,
                      fontSize: 38,
                      fontWeight: FontWeight.w700,
                      height: 1,
                    ),
                  ),
                  const SizedBox(height: 5),
                  Text(
                    l10n.t('records.lifetime',
                        {'count': '${client.lifetimeChars}'}),
                    style: TextStyle(
                      color: Colors.white.withValues(alpha: 0.82),
                      fontSize: 12,
                    ),
                  ),
                ],
              ),
              const Spacer(),
              Column(
                crossAxisAlignment: CrossAxisAlignment.end,
                children: [
                  Text(
                    '$minutes',
                    style: const TextStyle(
                      color: Colors.white,
                      fontSize: 28,
                      fontWeight: FontWeight.w700,
                      height: 1,
                    ),
                  ),
                  const SizedBox(height: 4),
                  Text(
                    l10n.t('records.saved', {'minutes': '$minutes'}),
                    style: TextStyle(
                      color: Colors.white.withValues(alpha: 0.82),
                      fontSize: 11,
                    ),
                  ),
                ],
              ),
            ],
          ),
        ),
        const SizedBox(height: 14),
        SwCard(
          title: l10n.t('popup.recent'),
          trailing: TextButton.icon(
            onPressed: () => client.openWindow('history'),
            icon: const Icon(Icons.open_in_new, size: 14),
            label: Text(l10n.t('records.open')),
          ),
          child: client.recent.isEmpty
              ? Padding(
                  padding: const EdgeInsets.symmetric(vertical: 18),
                  child: Text(
                    l10n.t('records.empty'),
                    style: Theme.of(context).textTheme.bodySmall,
                  ),
                )
              : Column(
                  children: [
                    for (final entry in client.recent)
                      _recentRow(l10n, entry),
                  ],
                ),
        ),
        const SizedBox(height: 14),
        const _PrivacyNote(),
      ],
    );
  }

  Widget _recentRow(L10n l10n, Map<String, dynamic> entry) {
    final text = entry['text']?.toString() ?? '';
    final mode = entry['mode']?.toString() ?? 'paste';
    final ts = (entry['timestampMs'] as num?)?.toInt() ?? 0;
    return Padding(
      padding: const EdgeInsets.only(bottom: 8),
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
        decoration: BoxDecoration(
          color: Theme.of(context).colorScheme.surfaceContainerHighest,
          borderRadius: BorderRadius.circular(10),
        ),
        child: Row(
          children: [
            SwModeBadge(mode: mode, label: modeLabel(mode, l10n.t)),
            const SizedBox(width: 10),
            Expanded(
              child: Text(
                text,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: Theme.of(context).textTheme.bodyMedium,
              ),
            ),
            const SizedBox(width: 8),
            Text(
              formatTime(ts),
              style: Theme.of(context).textTheme.bodySmall,
            ),
            IconButton(
              tooltip: l10n.t('common.copy'),
              onPressed: () {
                Clipboard.setData(ClipboardData(text: text));
                _snack(l10n.t('common.copied'));
              },
              icon: const Icon(Icons.copy, size: 15),
            ),
          ],
        ),
      ),
    );
  }

  Widget _permissions(L10n l10n) {
    final micName = (client.engine['micName'] as String?) ?? '';
    final micOk = micName.isNotEmpty;
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        SwPageHeader(
          title: l10n.t('nav.permissions'),
          subtitle: l10n.t('permissions.input.help'),
        ),
        const SizedBox(height: 22),
        SwCard(
          title: l10n.t('permissions.mic'),
          trailing: TextButton(
            onPressed: _checkingMic ? null : _loadDevices,
            child: Text(
              _checkingMic ? '…' : l10n.t('permissions.recheck'),
            ),
          ),
          child: Column(
            children: [
              Row(
                children: [
                  SwStatusDot(
                    color: micOk ? SwColors.success : SwColors.warn,
                  ),
                  const SizedBox(width: 8),
                  Expanded(
                    child: Text(
                      micOk ? micName : l10n.t('permissions.mic.denied'),
                      style: Theme.of(context).textTheme.bodyMedium,
                    ),
                  ),
                  SwPill(
                    text: micOk
                        ? l10n.t('permissions.mic.granted')
                        : (_micChecked
                            ? l10n.t('permissions.mic.denied')
                            : '—'),
                    color: micOk ? SwColors.success : SwColors.warn,
                  ),
                ],
              ),
              const SizedBox(height: 12),
              Align(
                alignment: Alignment.centerLeft,
                child: OutlinedButton.icon(
                  onPressed: () => Process.run('cmd', [
                    '/c',
                    'start',
                    'ms-settings:privacy-microphone',
                  ]),
                  icon: const Icon(Icons.open_in_new, size: 14),
                  label: Text(l10n.t('permissions.openSettings')),
                ),
              ),
            ],
          ),
        ),
        const SizedBox(height: 14),
        SwCard(
          title: l10n.t('permissions.input'),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                l10n.t('permissions.input.help'),
                style: Theme.of(context).textTheme.bodySmall,
              ),
              const SizedBox(height: 10),
              Row(
                children: [
                  SwPill(
                    text: client.hotkeyName,
                    color: SwColors.accentA,
                    icon: Icons.keyboard,
                  ),
                  const SizedBox(width: 8),
                  SwPill(
                    text: 'SendInput · Unicode',
                    color: SwColors.accentB,
                    icon: Icons.text_fields,
                  ),
                ],
              ),
            ],
          ),
        ),
        const SizedBox(height: 14),
        SwCard(
          title: l10n.t('permissions.test'),
          subtitle: l10n.t('permissions.test.help'),
          child: TextField(
            controller: _testController,
            maxLines: 4,
            decoration: InputDecoration(
              hintText: l10n.t('permissions.test.hint'),
            ),
          ),
        ),
      ],
    );
  }

  Widget _about(L10n l10n) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        SwPageHeader(
          title: l10n.t('nav.about'),
          subtitle: l10n.t('app.tagline'),
        ),
        const SizedBox(height: 22),
        SwCard(
          child: Row(
            children: [
              const SwBrandMark(size: 54),
              const SizedBox(width: 16),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                      '说文 ShuoWen',
                      style: Theme.of(context).textTheme.titleLarge,
                    ),
                    const SizedBox(height: 3),
                    Text(
                      l10n.t('about.version', {'version': '0.1.0'}),
                      style: Theme.of(context).textTheme.bodySmall,
                    ),
                    const SizedBox(height: 8),
                    Wrap(
                      spacing: 6,
                      runSpacing: 6,
                      children: [
                        SwPill(
                          text: l10n.t('about.local'),
                          color: SwColors.success,
                          icon: Icons.lock_outline,
                        ),
                        SwPill(
                          text: l10n.t('about.architecture'),
                          color: SwColors.accentA,
                        ),
                      ],
                    ),
                  ],
                ),
              ),
            ],
          ),
        ),
        const SizedBox(height: 14),
        SwCard(
          title: l10n.t('about.credits'),
          child: Column(
            children: [
              _aboutRow('sherpa-onnx', 'Apache-2.0'),
              const Divider(),
              _aboutRow('X-ASR (GilgameshWind/X-ASR-zh-en)', '模型'),
              const Divider(),
              _aboutRow('FireRedVAD · Silero VAD', 'VAD'),
              const Divider(),
              _aboutRow('Flutter · Windows', 'UI'),
            ],
          ),
        ),
        const SizedBox(height: 14),
        Row(
          children: [
            OutlinedButton.icon(
              onPressed: client.openDataDir,
              icon: const Icon(Icons.folder_open, size: 15),
              label: Text(l10n.t('about.dataDir')),
            ),
            const SizedBox(width: 10),
            OutlinedButton.icon(
              onPressed: () => _confirmQuit(l10n),
              icon: const Icon(Icons.power_settings_new, size: 15),
              label: Text(l10n.t('popup.quit')),
            ),
          ],
        ),
      ],
    );
  }

  Widget _aboutRow(String label, String value) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 8),
      child: Row(
        children: [
          Expanded(
            child: Text(label, style: Theme.of(context).textTheme.bodyMedium),
          ),
          Text(value, style: Theme.of(context).textTheme.bodySmall),
        ],
      ),
    );
  }

  Future<void> _confirmQuit(L10n l10n) async {
    final ok = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        backgroundColor: SwColors.surface,
        title: Text(l10n.t('popup.quit')),
        content: Text(l10n.t('general.tray.help')),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: Text(l10n.t('common.cancel')),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            child: Text(l10n.t('popup.quit')),
          ),
        ],
      ),
    );
    if (ok == true) await client.quit();
  }

  Widget _dropdown({
    required String value,
    required Map<String, String> items,
    required ValueChanged<String?> onChanged,
    double width = 190,
  }) {
    final safeValue = items.containsKey(value) ? value : items.keys.first;
    return SizedBox(
      width: width,
      child: Container(
        height: 38,
        padding: const EdgeInsets.symmetric(horizontal: 10),
        decoration: BoxDecoration(
          color: Theme.of(context).colorScheme.surfaceContainerHighest,
          borderRadius: BorderRadius.circular(9),
          border: Border.all(color: SwColors.hairline),
        ),
        child: DropdownButtonHideUnderline(
          child: DropdownButton<String>(
            value: safeValue,
            isExpanded: true,
            dropdownColor: SwColors.surface2,
            borderRadius: BorderRadius.circular(10),
            icon: const Icon(Icons.expand_more, size: 17),
            style: const TextStyle(
              color: SwColors.text,
              fontSize: 12.5,
              fontFamily: 'Segoe UI',
            ),
            items: items.entries
                .map((entry) => DropdownMenuItem<String>(
                      value: entry.key,
                      child: Text(
                        entry.value,
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                      ),
                    ))
                .toList(),
            onChanged: onChanged,
          ),
        ),
      ),
    );
  }
}

class _PrivacyNote extends StatelessWidget {
  const _PrivacyNote();

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        Icon(
          Icons.lock_outline,
          size: 14,
          color: Theme.of(context).textTheme.bodySmall?.color,
        ),
        const SizedBox(width: 7),
        Expanded(
          child: Text(
            L10nScope.of(context).t('about.local'),
            style: Theme.of(context).textTheme.bodySmall,
          ),
        ),
      ],
    );
  }
}
