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

class HistoryWindow extends StatefulWidget {
  const HistoryWindow({super.key, required this.client});

  final EngineClient client;

  @override
  State<HistoryWindow> createState() => _HistoryWindowState();
}

class _HistoryWindowState extends State<HistoryWindow> with WindowListener {
  final TextEditingController _search = TextEditingController();
  List<Map<String, dynamic>> _entries = [];
  bool _loading = true;
  String _query = '';
  int _lifetime = 0;
  Timer? _debounce;

  EngineClient get client => widget.client;

  @override
  void initState() {
    super.initState();
    windowManager.addListener(this);
    windowManager.setPreventClose(true);
    client.events.listen(_onEvent);
    _load();
  }

  @override
  void dispose() {
    windowManager.removeListener(this);
    _search.dispose();
    _debounce?.cancel();
    super.dispose();
  }

  @override
  void onWindowClose() {
    // History is opened on demand; do not keep a ~250 MB renderer alive while
    // the window is closed.
    exit(0);
  }

  void _onEvent(Map<String, dynamic> event) {
    if (event['event'] == 'shutdown') {
      exit(0);
    }
    if (event['event'] == 'focus' &&
        (event['data'] as Map?)?['window'] == 'history') {
      windowManager.show();
      windowManager.focus();
    }
    if (event['event'] == 'history' || event['event'] == 'final') {
      _debounce?.cancel();
      _debounce = Timer(const Duration(milliseconds: 250), _load);
    }
  }

  Future<void> _load() async {
    if (!mounted) return;
    setState(() => _loading = true);
    try {
      final data = await client.history(limit: 2000);
      if (!mounted) return;
      final list = data['entries'];
      setState(() {
        _entries = list is List
            ? list
                .whereType<Map>()
                .map((e) => Map<String, dynamic>.from(e))
                .toList()
            : [];
        _lifetime = (data['lifetimeChars'] as num?)?.toInt() ?? 0;
        _loading = false;
      });
    } catch (_) {
      if (!mounted) return;
      setState(() => _loading = false);
    }
  }

  List<Map<String, dynamic>> get _filtered {
    if (_query.trim().isEmpty) return _entries;
    final needle = _query.toLowerCase();
    return _entries.where((entry) {
      final text = (entry['text']?.toString() ?? '').toLowerCase();
      return text.contains(needle);
    }).toList();
  }

  @override
  Widget build(BuildContext context) {
    final l10n = L10nScope.of(context);
    final minutes = estimatedMinutes(_lifetime);
    final entries = _filtered;
    return Scaffold(
      backgroundColor: SwColors.bg,
      body: Column(
        children: [
          SwTitleBar(
            title: '说文 ShuoWen',
            subtitle: '${l10n.t('history.title')} · ${l10n.t('history.count', {'count': '${entries.length}'})}',
            trailing: SwPill(
              text: l10n.t('history.count', {'count': '${entries.length}'}),
              color: SwColors.accentA,
            ),
          ),
          SwConnectionBanner(client: client),
          Expanded(
            child: SingleChildScrollView(
              padding: const EdgeInsets.fromLTRB(32, 26, 32, 34),
              child: Align(
                alignment: Alignment.topCenter,
                child: ConstrainedBox(
                  constraints: const BoxConstraints(maxWidth: 980),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      SwPageHeader(
                        title: l10n.t('history.title'),
                        subtitle: l10n.t('history.privacy'),
                        trailing: Row(
                          mainAxisSize: MainAxisSize.min,
                          children: [
                            SizedBox(
                              width: 260,
                              height: 38,
                              child: TextField(
                                controller: _search,
                                onChanged: (value) =>
                                    setState(() => _query = value),
                                decoration: InputDecoration(
                                  hintText: l10n.t('history.search'),
                                  prefixIcon: const Icon(Icons.search, size: 17),
                                  contentPadding:
                                      const EdgeInsets.symmetric(vertical: 0),
                                ),
                              ),
                            ),
                            const SizedBox(width: 8),
                            OutlinedButton.icon(
                              onPressed: _export,
                              icon: const Icon(Icons.ios_share, size: 15),
                              label: Text(l10n.t('history.export')),
                            ),
                            const SizedBox(width: 6),
                            TextButton.icon(
                              onPressed: _entries.isEmpty ? null : _clearAll,
                              icon: const Icon(Icons.delete_sweep_outlined, size: 16),
                              label: Text(l10n.t('history.clear')),
                            ),
                          ],
                        ),
                      ),
                      const SizedBox(height: 20),
                      Container(
                        width: double.infinity,
                        padding: const EdgeInsets.all(20),
                        decoration: BoxDecoration(
                          gradient: SwTheme.accentGradient,
                          borderRadius: BorderRadius.circular(16),
                        ),
                        child: Row(
                          children: [
                            _stat('$_lifetime', l10n.t('history.lifetime',
                                {'count': '$_lifetime', 'minutes': '$minutes'})),
                            const SizedBox(width: 46),
                            _stat('$minutes', l10n.t('records.saved',
                                {'minutes': '$minutes'})),
                            const Spacer(),
                            Icon(
                              Icons.auto_awesome,
                              color: Colors.white.withValues(alpha: 0.85),
                              size: 30,
                            ),
                          ],
                        ),
                      ),
                      const SizedBox(height: 18),
                      if (_loading)
                        const Padding(
                          padding: EdgeInsets.symmetric(vertical: 60),
                          child: Center(
                            child: SizedBox(
                              width: 24,
                              height: 24,
                              child: CircularProgressIndicator(
                                strokeWidth: 2.4,
                                color: SwColors.accentB,
                              ),
                            ),
                          ),
                        )
                      else if (entries.isEmpty)
                        SwCard(
                          child: SwEmptyState(
                            icon: Icons.history_toggle_off,
                            title: l10n.t('history.empty'),
                            subtitle: l10n.t('records.privacy'),
                          ),
                        )
                      else
                        _entryList(l10n, entries),
                    ],
                  ),
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _stat(String value, String label) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          value,
          style: const TextStyle(
            color: Colors.white,
            fontSize: 32,
            fontWeight: FontWeight.w700,
            height: 1,
            fontFamily: 'Cascadia Mono',
          ),
        ),
        const SizedBox(height: 5),
        Text(
          label,
          style: TextStyle(
            color: Colors.white.withValues(alpha: 0.82),
            fontSize: 11.5,
          ),
        ),
      ],
    );
  }

  Widget _entryList(L10n l10n, List<Map<String, dynamic>> entries) {
    final children = <Widget>[];
    String? lastDay;
    for (final entry in entries) {
      final ts = (entry['timestampMs'] as num?)?.toInt() ?? 0;
      final day = dayLabel(
        ts,
        today: l10n.t('history.today'),
        yesterday: l10n.t('history.yesterday'),
      );
      if (day != lastDay) {
        lastDay = day;
        children.add(Padding(
          padding: EdgeInsets.only(top: children.isEmpty ? 0 : 18, bottom: 8),
          child: Text(
            day,
            style: TextStyle(
              color: Theme.of(context).textTheme.bodySmall?.color,
              fontSize: 11,
              fontWeight: FontWeight.w700,
              letterSpacing: 0.4,
            ),
          ),
        ));
      }
      children.add(_entryRow(l10n, entry));
    }
    return Column(children: children);
  }

  Widget _entryRow(L10n l10n, Map<String, dynamic> entry) {
    final id = entry['id']?.toString() ?? '';
    final text = entry['text']?.toString() ?? '';
    final mode = entry['mode']?.toString() ?? 'paste';
    final ts = (entry['timestampMs'] as num?)?.toInt() ?? 0;
    final ephemeral = entry['expiresAtMs'] != null;
    return Padding(
      padding: const EdgeInsets.only(bottom: 7),
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 12),
        decoration: BoxDecoration(
          color: Theme.of(context).colorScheme.surface,
          borderRadius: BorderRadius.circular(12),
          border: Border.all(color: SwColors.hairline),
        ),
        child: Row(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            SizedBox(
              width: 46,
              child: Text(
                formatTime(ts),
                style: TextStyle(
                  color: Theme.of(context).textTheme.bodySmall?.color,
                  fontSize: 11,
                  fontFamily: 'Cascadia Mono',
                ),
              ),
            ),
            SwModeBadge(mode: mode, label: modeLabel(mode, l10n.t)),
            const SizedBox(width: 12),
            Expanded(
              child: SelectableText(
                text,
                style: const TextStyle(
                  color: SwColors.text,
                  fontSize: 13,
                  height: 1.5,
                ),
              ),
            ),
            if (ephemeral)
              const Padding(
                padding: EdgeInsets.only(right: 6),
                child: SwPill(text: '60s', color: SwColors.warn),
              ),
            _rowAction(
              icon: Icons.copy,
              tooltip: l10n.t('common.copy'),
              onTap: () {
                Clipboard.setData(ClipboardData(text: text));
                _snack(l10n.t('common.copied'));
              },
            ),
            _rowAction(
              icon: Icons.edit_outlined,
              tooltip: l10n.t('history.edit'),
              onTap: () => _edit(id, text),
            ),
            _rowAction(
              icon: Icons.delete_outline,
              tooltip: l10n.t('common.delete'),
              onTap: () => _delete(id),
            ),
          ],
        ),
      ),
    );
  }

  Widget _rowAction({
    required IconData icon,
    required String tooltip,
    required VoidCallback onTap,
  }) {
    return IconButton(
      tooltip: tooltip,
      onPressed: onTap,
      iconSize: 16,
      visualDensity: VisualDensity.compact,
      icon: Icon(icon),
    );
  }

  void _snack(String message) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(content: Text(message), duration: const Duration(seconds: 2)),
    );
  }

  Future<void> _export() async {
    try {
      final path = await client.exportHistory();
      _snack('已导出：$path');
    } catch (error) {
      _snack(error.toString());
    }
  }

  Future<void> _clearAll() async {
    final l10n = L10nScope.of(context);
    final ok = await showDialog<bool>(
      context: context,
      builder: (context) => AlertDialog(
        backgroundColor: SwColors.surface,
        title: Text(l10n.t('history.clear')),
        content: Text(l10n.t('history.clear.confirm')),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context, false),
            child: Text(l10n.t('common.cancel')),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, true),
            child: Text(l10n.t('history.clear')),
          ),
        ],
      ),
    );
    if (ok == true) {
      await client.clearHistory();
      await _load();
    }
  }

  Future<void> _delete(String id) async {
    try {
      await client.deleteHistory(id);
      await _load();
    } catch (error) {
      _snack(error.toString());
    }
  }

  Future<void> _edit(String id, String text) async {
    final controller = TextEditingController(text: text);
    final l10n = L10nScope.of(context);
    final result = await showDialog<String>(
      context: context,
      builder: (context) => AlertDialog(
        backgroundColor: SwColors.surface,
        title: Text(l10n.t('history.edit')),
        content: SizedBox(
          width: 520,
          child: TextField(
            controller: controller,
            maxLines: 6,
            autofocus: true,
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: Text(l10n.t('common.cancel')),
          ),
          FilledButton(
            onPressed: () => Navigator.pop(context, controller.text),
            child: Text(l10n.t('common.save')),
          ),
        ],
      ),
    );
    controller.dispose();
    if (result == null) return;
    try {
      await client.updateHistory(id, result);
      await _load();
    } catch (error) {
      _snack(error.toString());
    }
  }
}
