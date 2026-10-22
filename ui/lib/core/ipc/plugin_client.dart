import 'dart:async';

import 'package:flutter/services.dart';

/// Flutter-side facade over the native named-pipe reader in the Windows runner.
/// No Dart FFI or background isolates: the pipe thread lives in C++, and lines
/// arrive on an EventChannel.
class PluginPipeClient {
  static const MethodChannel _methods = MethodChannel('shuowen/ipc');
  static const EventChannel _events = EventChannel('shuowen/ipc/events');

  Stream<String> get lines =>
      _events.receiveBroadcastStream().map((event) => event.toString());

  Future<bool> connect({String pipeName = r'\\.\pipe\shuowen.engine'}) async {
    try {
      return await _methods.invokeMethod<bool>('connect', pipeName) ?? false;
    } catch (_) {
      return false;
    }
  }

  Future<bool> send(String line) async {
    try {
      return await _methods.invokeMethod<bool>('send', line) ?? false;
    } catch (_) {
      return false;
    }
  }

  Future<void> dispose() async {
    try {
      await _methods.invokeMethod('disconnect');
    } catch (_) {
      // The engine may already be gone.
    }
  }
}
