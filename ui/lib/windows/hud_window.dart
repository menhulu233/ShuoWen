import 'dart:async';
import 'dart:io';

import 'package:flutter/material.dart';
import 'package:window_manager/window_manager.dart';

import '../core/engine_client.dart';
import '../core/l10n.dart';
import '../core/theme.dart';
import '../core/win32_window.dart';
import '../widgets/common.dart';

class HudWindow extends StatefulWidget {
  const HudWindow({super.key, required this.client});

  final EngineClient client;

  @override
  State<HudWindow> createState() => _HudWindowState();
}

class _HudWindowState extends State<HudWindow> with WindowListener {
  Timer? _hideTimer;
  bool _shown = false;
  String? _lastHudKey;

  EngineClient get client => widget.client;

  @override
  void initState() {
    super.initState();
    windowManager.addListener(this);
    client.addListener(_onClient);
    client.events.listen(_onEvent);
    WidgetsBinding.instance.addPostFrameCallback((_) => _onClient());
  }

  @override
  void dispose() {
    windowManager.removeListener(this);
    client.removeListener(_onClient);
    _hideTimer?.cancel();
    super.dispose();
  }

  @override
  void onWindowClose() {
    windowManager.hide();
  }

  void _onEvent(Map<String, dynamic> event) {
    if (event['event'] == 'shutdown') exit(0);
  }

  void _onClient() {
    if (!mounted) return;
    final hud = client.hud;
    final key = '${hud['visible']}|${hud['state']}|${hud['text']}';
    if (key == _lastHudKey) return;
    _lastHudKey = key;
    final visible = hud['visible'] == true;
    final state = hud['state']?.toString() ?? 'idle';
    _hideTimer?.cancel();
    if (visible) {
      _show();
      if (state == 'done') {
        final seconds =
            (client.settings['HudStaySeconds'] as num?)?.toDouble() ?? 0.5;
        _hideTimer = Timer(
          Duration(milliseconds: (seconds * 1000).round() + 250),
          _hide,
        );
      }
    } else {
      _hideTimer = Timer(const Duration(milliseconds: 220), _hide);
    }
  }

  Future<void> _show() async {
    if (!_shown) {
      _shown = true;
      await Win32Window.showHud();
      await windowManager.setAlwaysOnTop(true);
      await Win32Window.setClickThrough();
    }
  }

  Future<void> _hide() async {
    if (!_shown) return;
    _shown = false;
    await Win32Window.hide();
  }

  @override
  Widget build(BuildContext context) {
    final l10n = L10nScope.of(context);
    final hud = client.hud;
    final state = hud['state']?.toString() ?? 'idle';
    final visible = hud['visible'] == true;
    final text = (hud['text'] as String?) ?? '';
    final listening = state == 'listening';
    final done = state == 'done';
    final processing = state == 'processing';

    final title = done
        ? l10n.t('hud.insertedN', {'count': '${text.length}'})
        : processing
            ? l10n.t('hud.processing')
            : l10n.t('hud.listening');

    return Material(
      color: Colors.transparent,
      child: Container(
        decoration: BoxDecoration(
          color: SwColors.surface,
          borderRadius: BorderRadius.circular(26),
          border: Border.all(color: SwColors.hairlineStrong),
          boxShadow: [
            BoxShadow(
              color: Colors.black.withValues(alpha: 0.42),
              blurRadius: 30,
              offset: const Offset(0, 12),
            ),
          ],
        ),
        padding: const EdgeInsets.fromLTRB(18, 14, 18, 10),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Row(
              children: [
                _PulseOrb(
                  active: visible && (listening || processing),
                  done: done,
                ),
                const SizedBox(width: 14),
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      Row(
                        children: [
                          SwStatusDot(
                            color: done
                                ? SwColors.success
                                : listening
                                    ? SwColors.accentB
                                    : SwColors.warn,
                            pulse: listening,
                          ),
                          const SizedBox(width: 7),
                          Text(
                            title,
                            style: TextStyle(
                              color: done
                                  ? SwColors.success
                                  : SwColors.textMuted,
                              fontSize: 10.5,
                              fontWeight: FontWeight.w700,
                              letterSpacing: 0.3,
                            ),
                          ),
                          if (text.isNotEmpty) ...[
                            const SizedBox(width: 8),
                            Expanded(
                              child: Text(
                                text,
                                maxLines: 1,
                                overflow: TextOverflow.ellipsis,
                                textAlign: TextAlign.right,
                                style: const TextStyle(
                                  color: SwColors.text,
                                  fontSize: 13,
                                  fontWeight: FontWeight.w500,
                                ),
                              ),
                            ),
                          ],
                        ],
                      ),
                      const SizedBox(height: 3),
                      AnimatedSwitcher(
                        duration: const Duration(milliseconds: 180),
                        child: Text(
                          text.isEmpty
                              ? l10n.t('hud.listening')
                              : text,
                          key: ValueKey(text),
                          maxLines: 1,
                          overflow: TextOverflow.ellipsis,
                          style: TextStyle(
                            color: text.isEmpty
                                ? SwColors.textMuted
                                : SwColors.text,
                            fontSize: text.isEmpty ? 15 : 18,
                            fontWeight: FontWeight.w600,
                            letterSpacing: -0.1,
                          ),
                        ),
                      ),
                    ],
                  ),
                ),
                const SizedBox(width: 14),
                ValueListenableBuilder<double>(
                  valueListenable: client.levelNotifier,
                  builder: (context, level, _) => SwMeter(
                    level: listening || processing ? level : 0,
                    bars: 16,
                    height: 30,
                    color: done ? SwColors.success : SwColors.accentB,
                  ),
                ),
              ],
            ),
            const SizedBox(height: 6),
            Row(
              children: [
                const Spacer(),
                Text(
                  done
                      ? 'Esc 取消 · 可在设置中调整停留时间'
                      : l10n.t('hud.releaseHint'),
                  style: const TextStyle(
                    color: SwColors.textMuted,
                    fontSize: 9.5,
                  ),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}

class _PulseOrb extends StatefulWidget {
  const _PulseOrb({required this.active, required this.done});

  final bool active;
  final bool done;

  @override
  State<_PulseOrb> createState() => _PulseOrbState();
}

class _PulseOrbState extends State<_PulseOrb>
    with SingleTickerProviderStateMixin {
  late final AnimationController _controller = AnimationController(
    vsync: this,
    duration: const Duration(milliseconds: 1100),
  );

  @override
  void initState() {
    super.initState();
    if (widget.active) _controller.repeat(reverse: true);
  }

  @override
  void didUpdateWidget(covariant _PulseOrb oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (widget.active && !_controller.isAnimating) {
      _controller.repeat(reverse: true);
    } else if (!widget.active) {
      _controller.stop();
      _controller.value = 0;
    }
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return AnimatedBuilder(
      animation: _controller,
      builder: (context, _) {
        final scale = widget.active ? 1 + _controller.value * 0.07 : 1.0;
        return Transform.scale(
          scale: scale,
          child: Container(
            width: 44,
            height: 44,
            decoration: BoxDecoration(
              gradient: widget.done
                  ? const LinearGradient(
                      colors: [SwColors.success, SwColors.accentB],
                    )
                  : SwTheme.accentGradient,
              shape: BoxShape.circle,
              boxShadow: [
                BoxShadow(
                  color: (widget.done ? SwColors.success : SwColors.accentA)
                      .withValues(alpha: 0.35 + _controller.value * 0.2),
                  blurRadius: 16,
                  spreadRadius: 2,
                ),
              ],
            ),
            child: Icon(
              widget.done
                  ? Icons.check_rounded
                  : widget.active
                      ? Icons.graphic_eq_rounded
                      : Icons.mic_none_rounded,
              color: Colors.white,
              size: 21,
            ),
          ),
        );
      },
    );
  }
}
