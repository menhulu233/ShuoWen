import 'dart:async';
import 'dart:convert';

import 'package:flutter/foundation.dart';

import 'ipc/plugin_client.dart';

class EngineException implements Exception {
  EngineException(this.code, this.message);
  final String code;
  final String message;
  @override
  String toString() => '$code: $message';
}

class EngineClient extends ChangeNotifier {
  EngineClient(this.windowType);

  final String windowType;

  final Map<int, Completer<Map<String, dynamic>>> _pending = {};
  final StreamController<Map<String, dynamic>> _events =
      StreamController<Map<String, dynamic>>.broadcast();

  PluginPipeClient? _pipe;
  StreamSubscription<String>? _lineSub;
  Timer? _reconnectTimer;
  bool _disposed = false;
  bool _connecting = false;
  int _nextId = 1;
  bool connected = false;
  String? connectionError;
  bool shuttingDown = false;

  Map<String, dynamic> settings = {};
  Map<String, dynamic> engine = {};
  Map<String, dynamic> models = {};
  Map<String, dynamic> hud = {};
  Map<String, dynamic> oncall = {};
  List<Map<String, dynamic>> recent = [];
  int lifetimeChars = 0;
  int totalEntries = 0;
  double level = 0;
  String partial = '';
  String lastFinal = '';
  Map<String, dynamic>? toast;
  int toastSerial = 0;
  Map<String, dynamic>? hotkeyCaptured;
  int hotkeySerial = 0;
  final ValueNotifier<double> levelNotifier = ValueNotifier<double>(0);

  Stream<Map<String, dynamic>> get events => _events.stream;

  String get mode => (settings['Mode'] as String?) ?? 'Paste';
  String get language => (settings['Language'] as String?) ?? 'auto';
  String get engineStatus => (engine['status'] as String?) ?? 'starting';
  bool get listening => engine['listening'] == true;
  String get hotkeyName => (engine['hotkeyName'] as String?) ?? 'RightCtrl';
  bool get micMuted => (engine['micMuted'] as bool?) ?? false;

  Future<void> start() async {
    if (_disposed) return;
    _pipe ??= PluginPipeClient();
    _lineSub ??= _pipe!.lines.listen(_onPluginLine);
    // Let the EventChannel's listen handshake reach the native reader before
    // the first request, so the hello response cannot be dropped.
    await Future<void>.delayed(const Duration(milliseconds: 100));
    await _tryConnect();
    _reconnectTimer?.cancel();
    _reconnectTimer = Timer.periodic(const Duration(seconds: 1), (_) {
      if (!connected && !_disposed) _tryConnect();
    });
  }

  Future<void> _tryConnect() async {
    if (_connecting || _disposed || connected) return;
    _connecting = true;
    try {
      final pipe = _pipe!;
      final ok = await pipe.connect();
      if (!ok) {
        connectionError = '引擎未连接';
        if (!_disposed) notifyListeners();
        return;
      }
      connected = true;
      connectionError = null;
      notifyListeners();
      try {
        final snapshot = await request(
          'hello',
          {'window': windowType},
          const Duration(seconds: 5),
        );
        _applySnapshot(snapshot);
      } catch (error) {
        connected = false;
        connectionError = error.toString();
        notifyListeners();
      }
    } catch (error) {
      connectionError = error.toString();
      if (!_disposed) notifyListeners();
    } finally {
      _connecting = false;
    }
  }

  void _onPluginLine(String line) {
    if (line == '__shuowen_closed__') {
      _onClosed();
      return;
    }
    _onLine(line);
  }

  void _onClosed() {
    if (_disposed) return;
    connected = false;
    connectionError = '与引擎的连接已断开，正在重连…';
    for (final completer in _pending.values) {
      if (!completer.isCompleted) {
        completer.completeError(EngineException('disconnected', 'engine disconnected'));
      }
    }
    _pending.clear();
    notifyListeners();
  }

  void _onLine(String line) {
    dynamic decoded;
    try {
      decoded = jsonDecode(line);
    } catch (_) {
      return;
    }
    if (decoded is! Map) return;
    final map = Map<String, dynamic>.from(decoded);
    final id = map['id'];
    if (id is num) {
      final completer = _pending.remove(id.toInt());
      if (completer == null || completer.isCompleted) return;
      if (map['ok'] == true) {
        final data = map['data'];
        completer.complete(
            data is Map ? Map<String, dynamic>.from(data) : <String, dynamic>{});
      } else {
        final error = map['error'];
        final code = error is Map ? (error['code']?.toString() ?? 'error') : 'error';
        final message =
            error is Map ? (error['message']?.toString() ?? '') : error.toString();
        completer.completeError(EngineException(code, message));
      }
      return;
    }
    final event = map['event'];
    if (event is! String) return;
    final data = map['data'];
    _handleEvent(event,
        data is Map ? Map<String, dynamic>.from(data) : <String, dynamic>{});
  }

  void _handleEvent(String event, Map<String, dynamic> data) {
    switch (event) {
      case 'state':
        engine = data;
        break;
      case 'settings':
        settings = data;
        break;
      case 'models':
        models = data;
        break;
      case 'hud':
        hud = data;
        if (hud['visible'] != true) {
          level = 0;
          levelNotifier.value = 0;
        }
        break;
      case 'oncall':
        oncall = data;
        break;
      case 'partial':
        partial = (data['text'] as String?) ?? '';
        level = (data['level'] as num?)?.toDouble() ?? level;
        levelNotifier.value = level;
        break;
      case 'final':
        lastFinal = (data['text'] as String?) ?? '';
        if (lastFinal.isNotEmpty) partial = '';
        break;
      case 'level':
        level = (data['value'] as num?)?.toDouble() ?? 0;
        levelNotifier.value = level;
        _events.add({'event': event, 'data': data});
        return;
      case 'history':
        _refreshRecent();
        break;
      case 'toast':
        toast = data;
        toastSerial++;
        break;
      case 'hotkey.captured':
        hotkeyCaptured = data;
        hotkeySerial++;
        break;
      case 'shutdown':
        shuttingDown = true;
        break;
      default:
        break;
    }
    _events.add({'event': event, 'data': data});
    if (!_disposed) notifyListeners();
  }

  void _applySnapshot(Map<String, dynamic> snapshot) {
    settings = _map(snapshot['settings']);
    engine = _map(snapshot['engine']);
    models = _map(snapshot['models']);
    hud = _map(snapshot['hud']);
    oncall = _map(snapshot['oncall']);
    final history = _map(snapshot['history']);
    lifetimeChars = (history['lifetimeChars'] as num?)?.toInt() ?? 0;
    totalEntries = (history['count'] as num?)?.toInt() ?? 0;
    final list = history['recent'];
    recent = list is List
        ? list.whereType<Map>().map((e) => Map<String, dynamic>.from(e)).toList()
        : [];
    partial = (hud['text'] as String?) ?? '';
    notifyListeners();
  }

  static Map<String, dynamic> _map(dynamic value) =>
      value is Map ? Map<String, dynamic>.from(value) : <String, dynamic>{};

  Future<Map<String, dynamic>> request(
    String cmd, [
    Map<String, dynamic>? args,
    Duration timeout = const Duration(seconds: 30),
  ]) async {
    if (_pipe == null || !connected) {
      throw EngineException('disconnected', '引擎未连接');
    }
    final id = _nextId++;
    final completer = Completer<Map<String, dynamic>>();
    _pending[id] = completer;
    final payload = jsonEncode({'id': id, 'cmd': cmd, 'args': args ?? {}});
    final sent = await _pipe!.send(payload);
    if (!sent) {
      _pending.remove(id);
      throw EngineException('send_failed', '无法写入引擎管道');
    }
    return completer.future.timeout(timeout, onTimeout: () {
      _pending.remove(id);
      throw EngineException('timeout', '引擎响应超时');
    });
  }

  Future<void> patchSettings(Map<String, dynamic> patch) async {
    final data = await request('settings.patch', {'patch': patch});
    settings = data;
    notifyListeners();
  }

  Future<void> setMode(String mode) => patchSettings({'Mode': mode});

  Future<void> setModeCode(String code) => setMode(code);

  Future<void> setTrigger(String trigger) => patchSettings({'Trigger': trigger});

  Future<void> setTier(int ms) => patchSettings({'Tier': 'Ms$ms'});

  Future<void> setVad(String vad) => patchSettings({'Vad': vad});

  Future<void> setMicMuted(bool muted) => patchSettings({'MicMuted': muted});

  Future<void> captureHotkey() => request('hotkey.capture');

  Future<void> startDictation() => request('dictation.start');

  Future<void> stopDictation() => request('dictation.stop');

  Future<void> toggleDictation() => request('dictation.toggle');

  Future<void> pauseOnCall(bool paused) =>
      request('dictation.pause', {'paused': paused});

  Future<void> copyText([String? text]) async {
    await request('dictation.copy', text == null ? {} : {'text': text});
  }

  Future<void> clearOnCall() => request('dictation.clear');

  Future<Map<String, dynamic>> history({
    String query = '',
    int limit = 1000,
  }) =>
      request('history.list', {'query': query, 'limit': limit});

  Future<void> updateHistory(
    String id,
    String text, {
    String title = '',
    List<String> tags = const [],
  }) =>
      request('history.update',
          {'id': id, 'text': text, 'title': title, 'tags': tags});

  Future<void> deleteHistory(String id) =>
      request('history.delete', {'id': id});

  Future<void> clearHistory() => request('history.clear');

  Future<String> exportHistory() async {
    final data = await request('history.export');
    return (data['path'] as String?) ?? '';
  }

  Future<void> downloadTier(int tier) =>
      request('models.download', {'tier': tier});

  Future<void> downloadVad() => request('models.download_vad');

  Future<void> cancelDownload({int? tier, bool vad = false}) =>
      request('models.cancel', vad ? {'vad': true} : {'tier': tier ?? 0});

  Future<void> deleteTier(int tier) => request('models.delete', {'tier': tier});

  Future<void> useTier(int tier) => request('models.use', {'tier': tier});

  Future<void> reloadEngine() => request('engine.reload');

  Future<void> openWindow(String window) =>
      request('app.open', {'window': window});

  Future<void> openDataDir() => request('app.openDataDir');

  Future<void> quit() => request('app.quit');

  Future<List<Map<String, dynamic>>> micDevices() async {
    final data = await request('mic.devices');
    final list = data['devices'];
    return list is List
        ? list.whereType<Map>().map((e) => Map<String, dynamic>.from(e)).toList()
        : [];
  }

  Future<void> setMicDevice(String id) => patchSettings({'MicDeviceId': id});

  Future<void> _refreshRecent() async {
    try {
      final data = await history(limit: 5);
      lifetimeChars = (data['lifetimeChars'] as num?)?.toInt() ?? lifetimeChars;
      totalEntries = (data['count'] as num?)?.toInt() ?? totalEntries;
      final list = data['entries'];
      if (list is List) {
        recent = list
            .whereType<Map>()
            .map((e) => Map<String, dynamic>.from(e))
            .toList();
      }
    } catch (_) {
      // The next snapshot/reconnect will refresh it.
    }
  }

  @override
  void dispose() {
    _disposed = true;
    _reconnectTimer?.cancel();
    _lineSub?.cancel();
    _pipe?.dispose();
    for (final completer in _pending.values) {
      if (!completer.isCompleted) {
        completer.completeError(EngineException('disposed', 'client disposed'));
      }
    }
    _pending.clear();
    _events.close();
    levelNotifier.dispose();
    super.dispose();
  }
}
