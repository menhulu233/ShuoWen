import 'dart:async';
import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:window_manager/window_manager.dart';

import '../core/engine_client.dart';
import '../core/theme.dart';

class SwBrandMark extends StatelessWidget {
  const SwBrandMark({super.key, this.size = 36});

  final double size;

  @override
  Widget build(BuildContext context) {
    return Container(
      width: size,
      height: size,
      decoration: BoxDecoration(
        gradient: SwTheme.accentGradient,
        borderRadius: BorderRadius.circular(size * 0.28),
        boxShadow: [
          BoxShadow(
            color: SwColors.accentA.withValues(alpha: 0.28),
            blurRadius: 18,
            offset: const Offset(0, 6),
          ),
        ],
      ),
      alignment: Alignment.center,
      child: Text(
        '说',
        style: TextStyle(
          color: Colors.white,
          fontSize: size * 0.52,
          fontWeight: FontWeight.w700,
          height: 1,
        ),
      ),
    );
  }
}

class SwCard extends StatelessWidget {
  const SwCard({
    super.key,
    this.title,
    this.subtitle,
    this.child,
    this.padding = const EdgeInsets.all(18),
    this.trailing,
  });

  final String? title;
  final String? subtitle;
  final Widget? child;
  final EdgeInsets padding;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Container(
      width: double.infinity,
      decoration: BoxDecoration(
        color: theme.cardTheme.color ?? theme.colorScheme.surface,
        borderRadius: BorderRadius.circular(14),
        border: Border.all(color: theme.dividerTheme.color ?? Colors.white10),
      ),
      padding: padding,
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          if (title != null)
            Row(
              children: [
                Expanded(
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text(
                        title!,
                        style: theme.textTheme.titleMedium
                            ?.copyWith(fontWeight: FontWeight.w600),
                      ),
                      if (subtitle != null) ...[
                        const SizedBox(height: 3),
                        Text(
                          subtitle!,
                          style: theme.textTheme.bodySmall,
                        ),
                      ],
                    ],
                  ),
                ),
                ?trailing,
              ],
            ),
          if (title != null) const SizedBox(height: 14),
          ?child,
        ],
      ),
    );
  }
}

class SwSettingRow extends StatelessWidget {
  const SwSettingRow({
    super.key,
    required this.label,
    this.help,
    this.trailing,
    this.padding = const EdgeInsets.symmetric(vertical: 9),
  });

  final String label;
  final String? help;
  final Widget? trailing;
  final EdgeInsets padding;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Padding(
      padding: padding,
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.center,
        children: [
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(label, style: theme.textTheme.bodyMedium),
                if (help != null) ...[
                  const SizedBox(height: 3),
                  Text(help!, style: theme.textTheme.bodySmall),
                ],
              ],
            ),
          ),
          if (trailing != null) ...[
            const SizedBox(width: 16),
            trailing!,
          ],
        ],
      ),
    );
  }
}

class SwToggleRow extends StatelessWidget {
  const SwToggleRow({
    super.key,
    required this.label,
    this.help,
    required this.value,
    required this.onChanged,
  });

  final String label;
  final String? help;
  final bool value;
  final ValueChanged<bool>? onChanged;

  @override
  Widget build(BuildContext context) {
    return SwSettingRow(
      label: label,
      help: help,
      trailing: Switch(
        value: value,
        onChanged: onChanged,
        activeThumbColor: Colors.white,
        activeTrackColor: SwColors.accentA,
      ),
    );
  }
}

class SwPill extends StatelessWidget {
  const SwPill({
    super.key,
    required this.text,
    this.color,
    this.icon,
  });

  final String text;
  final Color? color;
  final IconData? icon;

  @override
  Widget build(BuildContext context) {
    final tint = color ?? SwColors.textMuted;
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 9, vertical: 4),
      decoration: BoxDecoration(
        color: tint.withValues(alpha: 0.12),
        borderRadius: BorderRadius.circular(999),
        border: Border.all(color: tint.withValues(alpha: 0.25)),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          if (icon != null) ...[
            Icon(icon, size: 12, color: tint),
            const SizedBox(width: 4),
          ],
          Text(
            text,
            style: TextStyle(
              color: tint,
              fontSize: 11,
              fontWeight: FontWeight.w600,
              height: 1.2,
            ),
          ),
        ],
      ),
    );
  }
}

class SwStatusDot extends StatelessWidget {
  const SwStatusDot({super.key, required this.color, this.pulse = false});

  final Color color;
  final bool pulse;

  @override
  Widget build(BuildContext context) {
    return Container(
      width: 9,
      height: 9,
      decoration: BoxDecoration(
        color: color,
        shape: BoxShape.circle,
        boxShadow: [
          BoxShadow(
            color: color.withValues(alpha: pulse ? 0.65 : 0.25),
            blurRadius: pulse ? 9 : 4,
            spreadRadius: pulse ? 2 : 0,
          ),
        ],
      ),
    );
  }
}

class SwGradientButton extends StatelessWidget {
  const SwGradientButton({
    super.key,
    required this.label,
    this.icon,
    this.onPressed,
    this.expand = false,
  });

  final String label;
  final IconData? icon;
  final VoidCallback? onPressed;
  final bool expand;

  @override
  Widget build(BuildContext context) {
    final enabled = onPressed != null;
    final child = Container(
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 11),
      decoration: BoxDecoration(
        gradient: enabled ? SwTheme.accentGradient : null,
        color: enabled ? null : SwColors.surface2,
        borderRadius: BorderRadius.circular(9),
      ),
      child: Row(
        mainAxisSize: expand ? MainAxisSize.max : MainAxisSize.min,
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          if (icon != null) ...[
            Icon(icon, size: 16, color: Colors.white),
            const SizedBox(width: 7),
          ],
          Text(
            label,
            style: TextStyle(
              color: enabled ? Colors.white : SwColors.textMuted,
              fontSize: 13,
              fontWeight: FontWeight.w600,
            ),
          ),
        ],
      ),
    );
    return MouseRegion(
      cursor: enabled ? SystemMouseCursors.click : SystemMouseCursors.basic,
      child: GestureDetector(onTap: onPressed, child: child),
    );
  }
}

class SwSegmentedOption<T> {
  const SwSegmentedOption({
    required this.value,
    required this.label,
    this.description,
  });

  final T value;
  final String label;
  final String? description;
}

class SwSegmented<T> extends StatelessWidget {
  const SwSegmented({
    super.key,
    required this.options,
    required this.value,
    required this.onChanged,
    this.showDescriptions = false,
  });

  final List<SwSegmentedOption<T>> options;
  final T value;
  final ValueChanged<T>? onChanged;
  final bool showDescriptions;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    if (showDescriptions) {
      return Column(
        children: [
          for (final option in options)
            Padding(
              padding: const EdgeInsets.only(bottom: 8),
              child: _DescriptionOption<T>(
                option: option,
                selected: option.value == value,
                onTap: onChanged == null ? null : () => onChanged!(option.value),
              ),
            ),
        ],
      );
    }
    return Container(
      padding: const EdgeInsets.all(3),
      decoration: BoxDecoration(
        color: theme.colorScheme.surfaceContainerHighest,
        borderRadius: BorderRadius.circular(10),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          for (final option in options)
            GestureDetector(
              onTap: onChanged == null ? null : () => onChanged!(option.value),
              child: AnimatedContainer(
                duration: const Duration(milliseconds: 160),
                curve: Curves.easeOut,
                padding:
                    const EdgeInsets.symmetric(horizontal: 13, vertical: 7),
                decoration: BoxDecoration(
                  color: option.value == value
                      ? theme.colorScheme.surface
                      : Colors.transparent,
                  borderRadius: BorderRadius.circular(8),
                  boxShadow: option.value == value
                      ? [
                          BoxShadow(
                            color: Colors.black.withValues(alpha: 0.12),
                            blurRadius: 6,
                            offset: const Offset(0, 2),
                          ),
                        ]
                      : null,
                ),
                child: Text(
                  option.label,
                  style: TextStyle(
                    fontSize: 12,
                    fontWeight: option.value == value
                        ? FontWeight.w600
                        : FontWeight.w500,
                    color: option.value == value
                        ? theme.colorScheme.onSurface
                        : theme.textTheme.bodySmall?.color,
                  ),
                ),
              ),
            ),
        ],
      ),
    );
  }
}

class _DescriptionOption<T> extends StatelessWidget {
  const _DescriptionOption({
    required this.option,
    required this.selected,
    this.onTap,
  });

  final SwSegmentedOption<T> option;
  final bool selected;
  final VoidCallback? onTap;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(10),
      child: AnimatedContainer(
        duration: const Duration(milliseconds: 160),
        padding: const EdgeInsets.symmetric(horizontal: 13, vertical: 11),
        decoration: BoxDecoration(
          color: selected
              ? SwColors.accentA.withValues(alpha: 0.10)
              : theme.colorScheme.surfaceContainerHighest,
          borderRadius: BorderRadius.circular(10),
          border: Border.all(
            color: selected
                ? SwColors.accentA.withValues(alpha: 0.45)
                : Colors.transparent,
          ),
        ),
        child: Row(
          children: [
            Icon(
              selected ? Icons.radio_button_checked : Icons.radio_button_off,
              size: 17,
              color: selected ? SwColors.accentA : theme.textTheme.bodySmall?.color,
            ),
            const SizedBox(width: 10),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    option.label,
                    style: TextStyle(
                      fontSize: 13,
                      fontWeight: FontWeight.w600,
                      color: theme.colorScheme.onSurface,
                    ),
                  ),
                  if (option.description != null) ...[
                    const SizedBox(height: 2),
                    Text(
                      option.description!,
                      style: theme.textTheme.bodySmall,
                    ),
                  ],
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class SwMeter extends StatelessWidget {
  const SwMeter({
    super.key,
    required this.level,
    this.bars = 22,
    this.height = 30,
    this.color = SwColors.accentB,
  });

  final double level;
  final int bars;
  final double height;
  final Color color;

  @override
  Widget build(BuildContext context) {
    return TweenAnimationBuilder<double>(
      tween: Tween(end: level.clamp(0.0, 1.0)),
      duration: const Duration(milliseconds: 90),
      builder: (context, value, _) {
        return SizedBox(
          height: height,
          child: Row(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.center,
            children: List.generate(bars, (index) {
              final phase = index / bars * math.pi * 2.2;
              final wave = (math.sin(phase + value * 7) + 1) / 2;
              final barHeight =
                  3 + value * height * (0.25 + 0.75 * wave);
              return Padding(
                padding: const EdgeInsets.symmetric(horizontal: 1.4),
                child: Container(
                  width: 2.6,
                  height: barHeight.clamp(3, height),
                  decoration: BoxDecoration(
                    color: color.withValues(alpha: 0.25 + 0.7 * value),
                    borderRadius: BorderRadius.circular(3),
                  ),
                ),
              );
            }),
          ),
        );
      },
    );
  }
}

class SwEmptyState extends StatelessWidget {
  const SwEmptyState({
    super.key,
    required this.icon,
    required this.title,
    this.subtitle,
  });

  final IconData icon;
  final String title;
  final String? subtitle;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(28),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            Icon(icon, size: 34, color: theme.textTheme.bodySmall?.color),
            const SizedBox(height: 10),
            Text(title, style: theme.textTheme.bodyMedium),
            if (subtitle != null) ...[
              const SizedBox(height: 4),
              Text(
                subtitle!,
                style: theme.textTheme.bodySmall,
                textAlign: TextAlign.center,
              ),
            ],
          ],
        ),
      ),
    );
  }
}

class SwProgressLine extends StatelessWidget {
  const SwProgressLine({super.key, required this.value});

  final double value;

  @override
  Widget build(BuildContext context) {
    return ClipRRect(
      borderRadius: BorderRadius.circular(999),
      child: LinearProgressIndicator(
        value: value.clamp(0.0, 1.0),
        minHeight: 5,
        backgroundColor:
            Theme.of(context).colorScheme.surfaceContainerHighest,
        valueColor: const AlwaysStoppedAnimation(SwColors.accentB),
      ),
    );
  }
}

class SwTitleBar extends StatelessWidget {
  const SwTitleBar({
    super.key,
    required this.title,
    this.subtitle,
    this.trailing,
    this.compact = false,
    this.onClose,
  });

  final String title;
  final String? subtitle;
  final Widget? trailing;
  final bool compact;
  final VoidCallback? onClose;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return GestureDetector(
      behavior: HitTestBehavior.opaque,
      onPanStart: (_) => windowManager.startDragging(),
      onDoubleTap: () async {
        if (await windowManager.isMaximized()) {
          await windowManager.unmaximize();
        } else {
          await windowManager.maximize();
        }
      },
      child: Container(
        height: compact ? 44 : 58,
        padding: EdgeInsets.symmetric(horizontal: compact ? 14 : 18),
        decoration: BoxDecoration(
          border: Border(
            bottom: BorderSide(
              color: theme.dividerTheme.color ?? Colors.white10,
            ),
          ),
        ),
        child: Row(
          children: [
            if (!compact) ...[
              const SwBrandMark(size: 26),
              const SizedBox(width: 10),
            ],
            Expanded(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    title,
                    style: TextStyle(
                      fontSize: compact ? 13 : 14,
                      fontWeight: FontWeight.w600,
                      color: theme.colorScheme.onSurface,
                    ),
                  ),
                  if (subtitle != null)
                    Text(
                      subtitle!,
                      style: theme.textTheme.bodySmall?.copyWith(fontSize: 11),
                    ),
                ],
              ),
            ),
            ?trailing,
            if (!compact) ...[
              const SizedBox(width: 8),
              _WindowButton(
                icon: Icons.remove,
                onTap: () => windowManager.minimize(),
              ),
              _WindowButton(
                icon: Icons.crop_square,
                onTap: () async {
                  if (await windowManager.isMaximized()) {
                    await windowManager.unmaximize();
                  } else {
                    await windowManager.maximize();
                  }
                },
              ),
            ],
            _WindowButton(
              icon: Icons.close,
              onTap: onClose ?? () => windowManager.hide(),
            ),
          ],
        ),
      ),
    );
  }
}

class _WindowButton extends StatelessWidget {
  const _WindowButton({required this.icon, required this.onTap});

  final IconData icon;
  final VoidCallback onTap;

  @override
  Widget build(BuildContext context) {
    return MouseRegion(
      cursor: SystemMouseCursors.click,
      child: GestureDetector(
        onTap: onTap,
        child: Container(
          width: 30,
          height: 28,
          margin: const EdgeInsets.only(left: 4),
          decoration: BoxDecoration(
            borderRadius: BorderRadius.circular(7),
          ),
          child: Icon(
            icon,
            size: 15,
            color: Theme.of(context).textTheme.bodySmall?.color,
          ),
        ),
      ),
    );
  }
}

class SwToastHost extends StatefulWidget {
  const SwToastHost({super.key, required this.client, required this.child});

  final EngineClient client;
  final Widget child;

  @override
  State<SwToastHost> createState() => _SwToastHostState();
}

class _SwToastHostState extends State<SwToastHost> {
  Map<String, dynamic>? _toast;
  Timer? _timer;
  int _serial = 0;

  @override
  void initState() {
    super.initState();
    _serial = widget.client.toastSerial;
    widget.client.addListener(_onClient);
  }

  @override
  void dispose() {
    widget.client.removeListener(_onClient);
    _timer?.cancel();
    super.dispose();
  }

  void _onClient() {
    if (widget.client.toastSerial == _serial) return;
    _serial = widget.client.toastSerial;
    _toast = widget.client.toast;
    setState(() {});
    _timer?.cancel();
    _timer = Timer(const Duration(seconds: 3), () {
      if (!mounted) return;
      setState(() => _toast = null);
    });
  }

  @override
  Widget build(BuildContext context) {
    final toast = _toast;
    final kind = toast?['kind']?.toString();
    final color = kind == 'error'
        ? SwColors.error
        : kind == 'success'
            ? SwColors.success
            : SwColors.warn;
    return Stack(
      children: [
        widget.child,
        Positioned(
          left: 0,
          right: 0,
          bottom: 22,
          child: Center(
            child: AnimatedSlide(
              offset: toast == null ? const Offset(0, 0.5) : Offset.zero,
              duration: const Duration(milliseconds: 220),
              child: AnimatedOpacity(
                opacity: toast == null ? 0 : 1,
                duration: const Duration(milliseconds: 220),
                child: IgnorePointer(
                  child: Container(
                    constraints: const BoxConstraints(maxWidth: 460),
                    margin: const EdgeInsets.symmetric(horizontal: 20),
                    padding: const EdgeInsets.symmetric(
                        horizontal: 14, vertical: 11),
                    decoration: BoxDecoration(
                      color: SwColors.surface2,
                      borderRadius: BorderRadius.circular(11),
                      border: Border.all(color: color.withValues(alpha: 0.35)),
                      boxShadow: [
                        BoxShadow(
                          color: Colors.black.withValues(alpha: 0.32),
                          blurRadius: 20,
                          offset: const Offset(0, 8),
                        ),
                      ],
                    ),
                    child: Row(
                      mainAxisSize: MainAxisSize.min,
                      children: [
                        Icon(
                          kind == 'error'
                              ? Icons.error_outline
                              : kind == 'success'
                                  ? Icons.check_circle_outline
                                  : Icons.info_outline,
                          size: 16,
                          color: color,
                        ),
                        const SizedBox(width: 8),
                        Flexible(
                          child: Text(
                            toast?['message']?.toString() ?? '',
                            style: const TextStyle(
                              color: SwColors.text,
                              fontSize: 12.5,
                            ),
                          ),
                        ),
                      ],
                    ),
                  ),
                ),
              ),
            ),
          ),
        ),
      ],
    );
  }
}

class SwConnectionBanner extends StatelessWidget {
  const SwConnectionBanner({super.key, required this.client});

  final EngineClient client;

  @override
  Widget build(BuildContext context) {
    if (client.connected) return const SizedBox.shrink();
    return Container(
      width: double.infinity,
      padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 9),
      color: SwColors.warn.withValues(alpha: 0.12),
      child: Row(
        children: [
          const Icon(Icons.cloud_off, size: 15, color: SwColors.warn),
          const SizedBox(width: 8),
          Expanded(
            child: Text(
              client.connectionError ?? '引擎未连接，正在重试…',
              style: const TextStyle(color: SwColors.warn, fontSize: 12),
            ),
          ),
        ],
      ),
    );
  }
}

class SwPageHeader extends StatelessWidget {
  const SwPageHeader({
    super.key,
    required this.title,
    this.subtitle,
    this.trailing,
  });

  final String title;
  final String? subtitle;
  final Widget? trailing;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Row(
      crossAxisAlignment: CrossAxisAlignment.end,
      children: [
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(title, style: theme.textTheme.titleLarge),
              if (subtitle != null) ...[
                const SizedBox(height: 4),
                Text(subtitle!, style: theme.textTheme.bodySmall),
              ],
            ],
          ),
        ),
        ?trailing,
      ],
    );
  }
}

class SwModeBadge extends StatelessWidget {
  const SwModeBadge({super.key, required this.mode, required this.label});

  final String mode;
  final String label;

  @override
  Widget build(BuildContext context) {
    final color = mode == 'oncall'
        ? SwColors.accentB
        : mode == 'type'
            ? SwColors.warn
            : SwColors.accentA;
    return SwPill(text: label, color: color);
  }
}

class SwSectionLabel extends StatelessWidget {
  const SwSectionLabel({super.key, required this.text});

  final String text;

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.only(left: 2, bottom: 8, top: 4),
      child: Text(
        text.toUpperCase(),
        style: TextStyle(
          fontSize: 10.5,
          fontWeight: FontWeight.w700,
          letterSpacing: 0.8,
          color: Theme.of(context).textTheme.bodySmall?.color,
        ),
      ),
    );
  }
}
