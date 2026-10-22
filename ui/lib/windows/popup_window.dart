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

class PopupWindow extends StatefulWidget {
  const PopupWindow({super.key, required this.client});

  final EngineClient client;

  @override
  State<PopupWindow> createState() => _PopupWindowState();
}

class _PopupWindowState extends State<PopupWindow> with WindowListener {
  EngineClient get client => widget.client;
  Timer? _retireTimer;

  @override
  void initState() {
    super.initState();
    windowManager.addListener(this);
    windowManager.setPreventClose(true);
    client.addListener(_onClient);
    client.events.listen(_onEvent);
  }

  @override
  void dispose() {
    _retireTimer?.cancel();
    windowManager.removeListener(this);
    client.removeListener(_onClient);
    super.dispose();
  }

  @override
  void onWindowBlur() {
    windowManager.hide();
    _retireTimer?.cancel();
    _retireTimer = Timer(const Duration(seconds: 10), () => exit(0));
  }

  @override
  void onWindowClose() {
    exit(0);
  }

  void _onClient() {
    if (mounted) setState(() {});
  }

  void _onEvent(Map<String, dynamic> event) {
    if (event['event'] == 'shutdown') exit(0);
    if (event['event'] == 'focus' &&
        (event['data'] as Map?)?['window'] == 'popup') {
      _retireTimer?.cancel();
      _retireTimer = null;
      windowManager.show();
      windowManager.focus();
    }
  }

  @override
  Widget build(BuildContext context) {
    final l10n = L10nScope.of(context);
    final engine = client.engine;
    final ready = client.engineStatus == 'ready';
    final listening = client.listening;
    final statusColor = ready
        ? listening
            ? SwColors.accentB
            : SwColors.success
        : client.engineStatus == 'error'
            ? SwColors.error
            : SwColors.warn;
    final statusText = client.engineStatus == 'ready'
        ? listening
            ? l10n.t('hud.listening')
            : l10n.t('common.ready')
        : client.engineStatus == 'error'
            ? l10n.t('common.error')
            : l10n.t('common.loading');
    final mode = client.mode;
    final recent = client.recent.isNotEmpty
        ? client.recent.first
        : (client.lastFinal.isNotEmpty
            ? {'text': client.lastFinal, 'mode': 'paste', 'timestampMs': 0}
            : null);
    final recentText = recent?['text']?.toString() ?? '';

    return Scaffold(
      backgroundColor: SwColors.surface,
      body: Column(
        children: [
          SwTitleBar(
            compact: true,
            title: l10n.t('popup.title'),
            subtitle: (engine['micName'] as String?) ?? '',
            trailing: IconButton(
              tooltip: l10n.t('popup.settings'),
              onPressed: () {
                client.openWindow('settings');
                windowManager.hide();
              },
              icon: const Icon(Icons.settings_outlined, size: 17),
            ),
          ),
          Expanded(
            child: SingleChildScrollView(
              padding: const EdgeInsets.fromLTRB(14, 12, 14, 14),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Container(
                    padding: const EdgeInsets.symmetric(
                        horizontal: 12, vertical: 11),
                    decoration: BoxDecoration(
                      color: Theme.of(context).colorScheme.surfaceContainerHighest,
                      borderRadius: BorderRadius.circular(11),
                    ),
                    child: Row(
                      children: [
                        SwStatusDot(color: statusColor, pulse: listening),
                        const SizedBox(width: 8),
                        Expanded(
                          child: Text(
                            statusText,
                            style: TextStyle(
                              color: statusColor,
                              fontSize: 12,
                              fontWeight: FontWeight.w700,
                            ),
                          ),
                        ),
                        SwPill(
                          text: client.hotkeyName,
                          color: SwColors.textMuted,
                          icon: Icons.keyboard,
                        ),
                      ],
                    ),
                  ),
                  const SizedBox(height: 10),
                  Row(
                    children: [
                      ValueListenableBuilder<double>(
                        valueListenable: client.levelNotifier,
                        builder: (context, level, _) => SwMeter(
                          level: client.micMuted ? 0 : level,
                          bars: 12,
                          height: 18,
                          color: SwColors.accentB,
                        ),
                      ),
                      const Spacer(),
                      Text(
                        client.micMuted
                            ? l10n.t('popup.mute')
                            : (engine['micName'] as String?) ?? '',
                        maxLines: 1,
                        overflow: TextOverflow.ellipsis,
                        style: Theme.of(context).textTheme.bodySmall,
                      ),
                    ],
                  ),
                  const SizedBox(height: 14),
                  SwSegmented<String>(
                    value: mode,
                    onChanged: (value) => client.setMode(value),
                    options: [
                      SwSegmentedOption(
                          value: 'Paste',
                          label: l10n.t('dictation.mode.paste')),
                      SwSegmentedOption(
                          value: 'Type', label: l10n.t('dictation.mode.type')),
                      SwSegmentedOption(
                          value: 'OnCall',
                          label: l10n.t('dictation.mode.oncall')),
                    ],
                  ),
                  if (mode == 'OnCall') ...[
                    const SizedBox(height: 8),
                    SizedBox(
                      width: double.infinity,
                      child: OutlinedButton.icon(
                        onPressed: () => client.pauseOnCall(
                            client.oncall['paused'] != true),
                        icon: Icon(
                          client.oncall['paused'] == true
                              ? Icons.play_arrow_rounded
                              : Icons.pause_rounded,
                          size: 15,
                        ),
                        label: Text(
                          client.oncall['paused'] == true
                              ? l10n.t('oncall.resume')
                              : l10n.t('oncall.pause'),
                        ),
                      ),
                    ),
                  ],
                  const SizedBox(height: 14),
                  SwSectionLabel(text: l10n.t('popup.recent')),
                  Container(
                    width: double.infinity,
                    padding: const EdgeInsets.all(12),
                    decoration: BoxDecoration(
                      color: Theme.of(context).colorScheme.surfaceContainerHighest,
                      borderRadius: BorderRadius.circular(11),
                      border: Border.all(color: SwColors.hairline),
                    ),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          recentText.isEmpty
                              ? '—'
                              : shorten(recentText, 120),
                          style: TextStyle(
                            color: recentText.isEmpty
                                ? SwColors.textMuted
                                : SwColors.text,
                            fontSize: 12.5,
                            height: 1.45,
                          ),
                        ),
                        if (recentText.isNotEmpty) ...[
                          const SizedBox(height: 8),
                          Row(
                            children: [
                              SwModeBadge(
                                mode: recent?['mode']?.toString() ?? 'paste',
                                label: modeLabel(
                                  recent?['mode']?.toString() ?? 'paste',
                                  l10n.t,
                                ),
                              ),
                              const Spacer(),
                              Text(
                                recent?['timestampMs'] != null
                                    ? formatTime(
                                        (recent?['timestampMs'] as num).toInt())
                                    : '',
                                style: Theme.of(context).textTheme.bodySmall,
                              ),
                              const SizedBox(width: 4),
                              IconButton(
                                tooltip: l10n.t('common.copy'),
                                onPressed: () {
                                  Clipboard.setData(
                                      ClipboardData(text: recentText));
                                  ScaffoldMessenger.of(context).showSnackBar(
                                    SnackBar(
                                      content: Text(l10n.t('common.copied')),
                                      duration: const Duration(seconds: 1),
                                    ),
                                  );
                                },
                                iconSize: 15,
                                visualDensity: VisualDensity.compact,
                                icon: const Icon(Icons.copy),
                              ),
                            ],
                          ),
                        ],
                      ],
                    ),
                  ),
                  const SizedBox(height: 14),
                  Row(
                    children: [
                      _action(
                        icon: Icons.history,
                        label: l10n.t('popup.history'),
                        onTap: () {
                          client.openWindow('history');
                          windowManager.hide();
                        },
                      ),
                      const SizedBox(width: 8),
                      _action(
                        icon: client.micMuted
                            ? Icons.mic_off_outlined
                            : Icons.mic_none_outlined,
                        label: l10n.t('popup.mute'),
                        active: client.micMuted,
                        onTap: () => client.setMicMuted(!client.micMuted),
                      ),
                      const SizedBox(width: 8),
                      _action(
                        icon: Icons.power_settings_new,
                        label: l10n.t('popup.quit'),
                        danger: true,
                        onTap: _confirmQuit,
                      ),
                    ],
                  ),
                  const SizedBox(height: 14),
                  Center(
                    child: Text(
                      l10n.t('about.local'),
                      style: const TextStyle(
                        color: SwColors.textMuted,
                        fontSize: 9.5,
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _action({
    required IconData icon,
    required String label,
    required VoidCallback onTap,
    bool active = false,
    bool danger = false,
  }) {
    final color = danger
        ? SwColors.error
        : active
            ? SwColors.accentB
            : SwColors.textMuted;
    return Expanded(
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(10),
        child: Container(
          padding: const EdgeInsets.symmetric(vertical: 10),
          decoration: BoxDecoration(
            color: color.withValues(alpha: 0.08),
            borderRadius: BorderRadius.circular(10),
            border: Border.all(color: color.withValues(alpha: 0.18)),
          ),
          child: Column(
            children: [
              Icon(icon, size: 18, color: color),
              const SizedBox(height: 4),
              Text(
                label,
                maxLines: 1,
                overflow: TextOverflow.ellipsis,
                style: TextStyle(
                  color: color,
                  fontSize: 10,
                  fontWeight: FontWeight.w600,
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Future<void> _confirmQuit() async {
    final l10n = L10nScope.of(context);
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
}
