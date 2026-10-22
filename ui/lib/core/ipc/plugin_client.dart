import 'dart:async';

import 'package:flutter/services.dart';

/// Seam for EngineClient tests. The production implementation wraps the
/// native named-pipe client in the Windows runner.
abstract class PipeClient {
  Stream<String> get lines;
  Future<bool> connect({String pipeName});
  Future<bool> send(String line);
  Future<void> dispose();
}

/// Flutter-side facade over the native named-pipe reader in the Windows runner.
/// No Dart FFI or background isolates: the pipe thread lives in C++, and lines
/// arrive on an EventChannel.
class PluginPipeClient implements PipeClient {
  static const MethodChannel _methods = MethodChannel('shuowen/ipc');
  static const EventChannel _events = EventChannel('shuowen/ipc/events');

  @override
  Stream<String> get lines =>
      _events.receiveBroadcastStream().map((event) => event.toString());

  @override
  Future<bool> connect({String pipeName = r'\\.\pipe\shuowen.engine'}) async {
    try {
      return await _methods.invokeMethod<bool>('connect', pipeName) ?? false;
    } catch (_) {
      return false;
    }
  }

  @override
  Future<bool> send(String line) async {
    try {
      return await _methods.invokeMethod<bool>('send', line) ?? false;
    } catch (_) {
      return false;
    }
  }

  @override
  Future<void> dispose() async {
    try {
      await _methods.invokeMethod('disconnect');
    } catch (_) {
      // The engine may already be gone.
    }
  }
}
