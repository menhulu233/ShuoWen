import 'dart:async';

import 'package:flutter/material.dart';

import '../core/engine_client.dart';
import '../core/format.dart';
import '../core/l10n.dart';
import '../core/theme.dart';
import '../widgets/common.dart';

class OnboardingView extends StatefulWidget {
  const OnboardingView({super.key, required this.client});

  final EngineClient client;

  @override
  State<OnboardingView> createState() => _OnboardingViewState();
}

class _OnboardingViewState extends State<OnboardingView> {
  bool _recording = false;
  int _hotkeySerial = 0;
  int _deviceCount = 0;
  bool _checkingMic = false;

  @override
  void initState() {
    super.initState();
    _hotkeySerial = widget.client.hotkeySerial;
    widget.client.addListener(_onClient);
  }

  @override
  void dispose() {
    widget.client.removeListener(_onClient);
    super.dispose();
  }

  void _onClient() {
    if (widget.client.hotkeySerial != _hotkeySerial) {
      _hotkeySerial = widget.client.hotkeySerial;
      if (_recording && mounted) {
        setState(() => _recording = false);
      }
    }
  }

  Future<void> _checkMic() async {
    setState(() => _checkingMic = true);
    try {
      final devices = await widget.client.micDevices();
      if (!mounted) return;
      setState(() => _deviceCount = devices.length);
    } catch (_) {
      if (mounted) setState(() => _deviceCount = 0);
    } finally {
      if (mounted) setState(() => _checkingMic = false);
    }
  }

  Future<void> _patch(Map<String, dynamic> patch) async {
    try {
      await widget.client.patchSettings(patch);
    } catch (error) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(error.toString())),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    final l10n = L10nScope.of(context);
    final settings = widget.client.settings;
    final engine = widget.client.engine;
    final models = widget.client.models;
    final ready = widget.client.engineStatus == 'ready';
    final micName = (engine['micName'] as String?) ?? '';
    final hotkey = hotkeyLabel(
      (settings['HotkeyVk'] as num?)?.toInt() ?? 0xA3,
      (settings['HotkeyMods'] as num?)?.toInt() ?? 0,
      l10n.t,
    );

    final tiers = models['tiers'];
    Map<String, dynamic>? tier960;
    if (tiers is List) {
      for (final item in tiers) {
        if (item is Map && (item['tier'] as num?)?.toInt() == 960) {
          tier960 = Map<String, dynamic>.from(item);
        }
      }
    }
    final modelState = tier960?['state']?.toString() ?? 'missing';

    return Center(
      child: SingleChildScrollView(
        padding: const EdgeInsets.symmetric(horizontal: 34, vertical: 30),
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: 760),
          child: Column(
            children: [
              const SwBrandMark(size: 64),
              const SizedBox(height: 20),
              Text(
                l10n.t('onboarding.title'),
                style: Theme.of(context).textTheme.titleLarge?.copyWith(
                      fontSize: 28,
                      letterSpacing: -0.5,
                    ),
              ),
              const SizedBox(height: 8),
              Text(
                l10n.t('onboarding.subtitle'),
                textAlign: TextAlign.center,
                style: Theme.of(context).textTheme.bodyMedium?.copyWith(
                      color: Theme.of(context).textTheme.bodySmall?.color,
                    ),
              ),
              const SizedBox(height: 28),
              _StepCard(
                index: 1,
                title: l10n.t('onboarding.step1'),
                state: micName.isNotEmpty
                    ? '✓ $micName'
                    : _deviceCount > 0
                        ? '✓ ${l10n.t('permissions.mic.granted')}'
                        : l10n.t('permissions.mic.denied'),
                stateColor: micName.isNotEmpty || _deviceCount > 0
                    ? SwColors.success
                    : SwColors.warn,
                action: TextButton(
                  onPressed: _checkingMic ? null : _checkMic,
                  child: Text(_checkingMic ? '…' : l10n.t('permissions.recheck')),
                ),
              ),
              const SizedBox(height: 10),
              _StepCard(
                index: 2,
                title: l10n.t('onboarding.step2'),
                state: _recording
                    ? l10n.t('dictation.hotkey.recording')
                    : hotkey,
                stateColor: _recording ? SwColors.accentB : SwColors.success,
                action: TextButton(
                  onPressed: () async {
                    setState(() => _recording = true);
                    await widget.client.captureHotkey();
                  },
                  child: Text(_recording ? '…' : l10n.t('dictation.hotkey')),
                ),
              ),
              const SizedBox(height: 10),
              _StepCard(
                index: 3,
                title: l10n.t('onboarding.step3'),
                state: ready
                    ? l10n.t('common.ready')
                    : modelState == 'downloading'
                        ? l10n.t('common.loading')
                        : modelState == 'missing'
                            ? l10n.t('common.missing')
                            : l10n.t('common.loading'),
                stateColor: ready
                    ? SwColors.success
                    : modelState == 'error'
                        ? SwColors.error
                        : SwColors.warn,
                action: ready
                    ? const Icon(Icons.check_circle, color: SwColors.success)
                    : modelState == 'missing'
                        ? TextButton(
                            onPressed: () =>
                                widget.client.downloadTier(960),
                            child: Text(l10n.t('common.download')),
                          )
                        : const SizedBox(
                            width: 16,
                            height: 16,
                            child: CircularProgressIndicator(
                              strokeWidth: 2,
                              color: SwColors.accentB,
                            ),
                          ),
              ),
              const SizedBox(height: 26),
              SwGradientButton(
                label: l10n.t('onboarding.start'),
                icon: Icons.arrow_forward,
                expand: true,
                onPressed: () => _patch({'Welcomed': true}),
              ),
              const SizedBox(height: 12),
              Text(
                l10n.t('about.local'),
                style: Theme.of(context).textTheme.bodySmall,
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _StepCard extends StatelessWidget {
  const _StepCard({
    required this.index,
    required this.title,
    required this.state,
    required this.stateColor,
    required this.action,
  });

  final int index;
  final String title;
  final String state;
  final Color stateColor;
  final Widget action;

  @override
  Widget build(BuildContext context) {
    final theme = Theme.of(context);
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 16),
      decoration: BoxDecoration(
        color: theme.colorScheme.surface,
        borderRadius: BorderRadius.circular(13),
        border: Border.all(color: theme.dividerTheme.color ?? Colors.white10),
      ),
      child: Row(
        children: [
          Container(
            width: 26,
            height: 26,
            alignment: Alignment.center,
            decoration: BoxDecoration(
              gradient: SwTheme.accentGradient,
              borderRadius: BorderRadius.circular(8),
            ),
            child: Text(
              '$index',
              style: const TextStyle(
                color: Colors.white,
                fontSize: 12,
                fontWeight: FontWeight.w700,
              ),
            ),
          ),
          const SizedBox(width: 13),
          Expanded(
            child: Padding(
              padding: const EdgeInsets.symmetric(vertical: 15),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(title, style: theme.textTheme.bodyMedium),
                  const SizedBox(height: 3),
                  Text(
                    state,
                    style: TextStyle(
                      color: stateColor,
                      fontSize: 12,
                      fontWeight: FontWeight.w600,
                    ),
                  ),
                ],
              ),
            ),
          ),
          action,
        ],
      ),
    );
  }
}
