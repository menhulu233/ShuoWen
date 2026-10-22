import 'dart:io';

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:window_manager/window_manager.dart';

import '../core/engine_client.dart';
import '../core/l10n.dart';
import '../core/theme.dart';
import '../widgets/common.dart';

class OnCallWindow extends StatefulWidget {
  const OnCallWindow({super.key, required this.client});

  final EngineClient client;

  @override
  State<OnCallWindow> createState() => _OnCallWindowState();
}

class _OnCallWindowState extends State<OnCallWindow> with WindowListener {
  EngineClient get client => widget.client;

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
    windowManager.removeListener(this);
    client.removeListener(_onClient);
    super.dispose();
  }

  @override
  void onWindowClose() {
    windowManager.hide();
  }

  void _onClient() {
    if (mounted) setState(() {});
  }

  void _onEvent(Map<String, dynamic> event) {
    if (event['event'] == 'shutdown') exit(0);
    if (event['event'] == 'focus' &&
        (event['data'] as Map?)?['window'] == 'oncall') {
      windowManager.show();
      windowManager.focus();
    }
  }

  @override
  Widget build(BuildContext context) {
    final l10n = L10nScope.of(context);
    final onCallMode = client.mode == 'OnCall';
    final paused = client.oncall['paused'] == true;
    final rawSegments = client.oncall['segments'];
    final segments = rawSegments is List
        ? rawSegments.map((e) => e.toString()).toList()
        : <String>[];
    final partial = (client.oncall['partial'] as String?) ?? '';
    final count = segments.length;

    return Scaffold(
      backgroundColor: SwColors.surface,
      body: Column(
        children: [
          SwTitleBar(
            compact: true,
            title: l10n.t('oncall.title'),
            subtitle: onCallMode
                ? paused
                    ? l10n.t('oncall.paused')
                    : l10n.t('oncall.listening')
                : '未启用',
            trailing: Row(
              mainAxisSize: MainAxisSize.min,
              children: [
                if (onCallMode)
                  IconButton(
                    tooltip: paused
                        ? l10n.t('oncall.resume')
                        : l10n.t('oncall.pause'),
                    onPressed: () => client.pauseOnCall(!paused),
                    icon: Icon(
                      paused
                          ? Icons.play_arrow_rounded
                          : Icons.pause_rounded,
                      size: 18,
                    ),
                  ),
                IconButton(
                  tooltip: l10n.t('popup.settings'),
                  onPressed: () => client.openWindow('settings'),
                  icon: const Icon(Icons.settings_outlined, size: 17),
                ),
              ],
            ),
          ),
          Expanded(
            child: onCallMode
                ? _session(l10n, segments, partial, paused, count)
                : _inactive(l10n),
          ),
        ],
      ),
    );
  }

  Widget _inactive(L10n l10n) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Container(
              width: 54,
              height: 54,
              decoration: BoxDecoration(
                gradient: SwTheme.accentGradient,
                shape: BoxShape.circle,
              ),
              child: const Icon(
                Icons.graphic_eq_rounded,
                color: Colors.white,
                size: 26,
              ),
            ),
            const SizedBox(height: 14),
            Text(
              l10n.t('oncall.empty'),
              textAlign: TextAlign.center,
              style: Theme.of(context).textTheme.bodySmall,
            ),
            const SizedBox(height: 16),
            SwGradientButton(
              label: l10n.t('oncall.switch'),
              icon: Icons.mic,
              onPressed: () => client.setMode('OnCall'),
            ),
          ],
        ),
      ),
    );
  }

  Widget _session(
    L10n l10n,
    List<String> segments,
    String partial,
    bool paused,
    int count,
  ) {
    return Column(
      children: [
        Padding(
          padding: const EdgeInsets.fromLTRB(16, 12, 16, 8),
          child: Row(
            children: [
              SwStatusDot(
                color: paused ? SwColors.warn : SwColors.accentB,
                pulse: !paused,
              ),
              const SizedBox(width: 7),
              Text(
                paused ? l10n.t('oncall.paused') : l10n.t('oncall.listening'),
                style: TextStyle(
                  color: paused ? SwColors.warn : SwColors.accentB,
                  fontSize: 11,
                  fontWeight: FontWeight.w700,
                ),
              ),
              const Spacer(),
              SwPill(
                text: l10n.t('history.count', {'count': '$count'}),
                color: SwColors.textMuted,
              ),
              const SizedBox(width: 6),
              ValueListenableBuilder<double>(
                valueListenable: client.levelNotifier,
                builder: (context, level, _) => SwMeter(
                  level: paused ? 0 : level,
                  bars: 12,
                  height: 20,
                  color: SwColors.accentB,
                ),
              ),
            ],
          ),
        ),
        Expanded(
          child: segments.isEmpty && partial.isEmpty
              ? SwEmptyState(
                  icon: Icons.record_voice_over_outlined,
                  title: l10n.t('oncall.empty'),
                )
              : ListView(
                  padding: const EdgeInsets.fromLTRB(14, 0, 14, 8),
                  children: [
                    for (var i = 0; i < segments.length; i++)
                      _segment(l10n, segments[i], i + 1),
                    if (partial.isNotEmpty) _partial(l10n, partial),
                  ],
                ),
        ),
        Container(
          padding: const EdgeInsets.fromLTRB(14, 8, 14, 12),
          decoration: const BoxDecoration(
            border: Border(top: BorderSide(color: SwColors.hairline)),
          ),
          child: Row(
            children: [
              TextButton.icon(
                onPressed: segments.isEmpty
                    ? null
                    : () {
                        Clipboard.setData(
                          ClipboardData(text: segments.join('\n')),
                        );
                        ScaffoldMessenger.of(context).showSnackBar(
                          SnackBar(
                            content: Text(l10n.t('common.copied')),
                            duration: const Duration(seconds: 1),
                          ),
                        );
                      },
                icon: const Icon(Icons.copy_all_outlined, size: 15),
                label: Text(l10n.t('oncall.copyAll')),
              ),
              TextButton.icon(
                onPressed: segments.isEmpty ? null : client.clearOnCall,
                icon: const Icon(Icons.delete_sweep_outlined, size: 15),
                label: Text(l10n.t('oncall.clear')),
              ),
              const Spacer(),
              const Icon(
                Icons.lock_outline,
                size: 12,
                color: SwColors.textMuted,
              ),
              const SizedBox(width: 4),
              const Text(
                'local',
                style: TextStyle(color: SwColors.textMuted, fontSize: 9.5),
              ),
            ],
          ),
        ),
      ],
    );
  }

  Widget _segment(L10n l10n, String text, int index) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 7),
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
        decoration: BoxDecoration(
          color: Theme.of(context).colorScheme.surfaceContainerHighest,
          borderRadius: BorderRadius.circular(10),
        ),
        child: Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Container(
              width: 20,
              height: 20,
              alignment: Alignment.center,
              decoration: BoxDecoration(
                color: SwColors.accentA.withValues(alpha: 0.14),
                borderRadius: BorderRadius.circular(6),
              ),
              child: Text(
                '$index',
                style: const TextStyle(
                  color: SwColors.accentA,
                  fontSize: 9.5,
                  fontWeight: FontWeight.w700,
                ),
              ),
            ),
            const SizedBox(width: 9),
            Expanded(
              child: SelectableText(
                text,
                style: const TextStyle(
                  color: SwColors.text,
                  fontSize: 12.5,
                  height: 1.45,
                ),
              ),
            ),
            IconButton(
              tooltip: l10n.t('common.copy'),
              onPressed: () {
                Clipboard.setData(ClipboardData(text: text));
              },
              iconSize: 14,
              visualDensity: VisualDensity.compact,
              icon: const Icon(Icons.copy, size: 14),
            ),
          ],
        ),
      ),
    );
  }

  Widget _partial(L10n l10n, String text) {
    return Container(
      margin: const EdgeInsets.only(bottom: 7),
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
      decoration: BoxDecoration(
        color: SwColors.accentSoft,
        borderRadius: BorderRadius.circular(10),
        border: Border.all(color: SwColors.accentA.withValues(alpha: 0.25)),
      ),
      child: Row(
        children: [
          const SizedBox(
            width: 13,
            height: 13,
            child: CircularProgressIndicator(
              strokeWidth: 1.8,
              color: SwColors.accentB,
            ),
          ),
          const SizedBox(width: 9),
          Expanded(
            child: Text(
              text,
              style: const TextStyle(
                color: SwColors.text,
                fontSize: 12.5,
                height: 1.45,
              ),
            ),
          ),
        ],
      ),
    );
  }
}
