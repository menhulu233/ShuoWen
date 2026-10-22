import 'dart:async';
import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';
import 'package:shuowen_ui/core/engine_client.dart';
import 'package:shuowen_ui/core/ipc/plugin_client.dart';

/// In-memory pipe that answers requests from [responder] and can push
/// arbitrary events back to the client.
class FakePipe implements PipeClient {
  final StreamController<String> _lines = StreamController<String>.broadcast();
  final List<Map<String, dynamic>> sent = [];
  Map<String, dynamic>? Function(String cmd, int id)? responder;

  @override
  Stream<String> get lines => _lines.stream;

  @override
  Future<bool> connect({
    String pipeName = r'\\.\pipe\shuowen.engine',
  }) async =>
      true;

  @override
  Future<bool> send(String line) async {
    final map = jsonDecode(line) as Map<String, dynamic>;
    sent.add(map);
    final response = responder?.call(map['cmd'] as String, map['id'] as int);
    if (response != null) {
      scheduleMicrotask(() => _lines.add(jsonEncode(response)));
    }
    return true;
  }

  @override
  Future<void> dispose() async {
    await _lines.close();
  }

  void emit(Map<String, dynamic> event) => _lines.add(jsonEncode(event));

  void emitRaw(String line) => _lines.add(line);
}

Map<String, dynamic> snapshotResponse(int id) => {
      'id': id,
      'ok': true,
      'data': {
        'settings': {'Mode': 'Paste', 'Tier': 960},
        'engine': {'status': 'ready', 'hotkeyName': 'RightCtrl'},
        'models': {'tiers': <dynamic>[]},
        'hud': {'visible': false, 'state': 'idle'},
        'oncall': {'segments': <dynamic>[]},
        'history': {'lifetimeChars': 42, 'count': 3, 'recent': <dynamic>[]},
      },
    };

void main() {
  test('hello snapshot populates client state', () async {
    final pipe = FakePipe();
    pipe.responder = (cmd, id) => snapshotResponse(id);
    final client = EngineClient.withPipe('settings', pipe);

    await client.start();

    expect(client.connected, isTrue);
    expect(client.settings['Mode'], 'Paste');
    expect(client.engineStatus, 'ready');
    expect(client.hotkeyName, 'RightCtrl');
    expect(client.lifetimeChars, 42);
    expect(client.totalEntries, 3);
    expect(pipe.sent.first['cmd'], 'hello');

    client.dispose();
  });

  test('events update state and level notifier', () async {
    final pipe = FakePipe();
    pipe.responder = (cmd, id) => snapshotResponse(id);
    final client = EngineClient.withPipe('hud', pipe);

    await client.start();

    pipe.emit({
      'event': 'partial',
      'data': {'text': '你好', 'level': 0.5},
    });
    await Future<void>.delayed(Duration.zero);
    expect(client.partial, '你好');
    expect(client.levelNotifier.value, closeTo(0.5, 0.001));

    pipe.emit({'event': 'toast', 'data': {'kind': 'error', 'message': 'x'}});
    await Future<void>.delayed(Duration.zero);
    expect(client.toastSerial, 1);
    expect(client.toast?['kind'], 'error');

    pipe.emit({'event': 'hud', 'data': {'visible': false, 'state': 'idle'}});
    await Future<void>.delayed(Duration.zero);
    expect(client.levelNotifier.value, 0);

    client.dispose();
  });

  test('error responses surface as EngineException', () async {
    final pipe = FakePipe();
    pipe.responder = (cmd, id) => snapshotResponse(id);
    final client = EngineClient.withPipe('settings', pipe);

    await client.start();

    pipe.responder = (cmd, id) => {
          'id': id,
          'ok': false,
          'error': {'code': 'bad_args', 'message': 'nope'},
        };
    await expectLater(
      client.request('settings.patch', {'patch': 1}),
      throwsA(isA<EngineException>()
          .having((e) => e.code, 'code', 'bad_args')),
    );

    client.dispose();
  });

  test('closed pipe marks the client disconnected', () async {
    final pipe = FakePipe();
    pipe.responder = (cmd, id) => snapshotResponse(id);
    final client = EngineClient.withPipe('hud', pipe);

    await client.start();
    expect(client.connected, isTrue);

    pipe.emitRaw('__shuowen_closed__');
    await Future<void>.delayed(Duration.zero);

    expect(client.connected, isFalse);
    expect(client.connectionError, isNotEmpty);

    client.dispose();
  });
}
