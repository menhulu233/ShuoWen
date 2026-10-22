import 'package:flutter/material.dart';

String two(int value) => value.toString().padLeft(2, '0');

DateTime dateTimeFromMs(int ms) =>
    DateTime.fromMillisecondsSinceEpoch(ms, isUtc: false).toLocal();

String formatTime(int ms) {
  final dt = dateTimeFromMs(ms);
  return '${two(dt.hour)}:${two(dt.minute)}';
}

String formatDate(int ms) {
  final dt = dateTimeFromMs(ms);
  return '${dt.year}-${two(dt.month)}-${two(dt.day)}';
}

String formatDateTime(int ms) => '${formatDate(ms)} ${formatTime(ms)}';

String dayLabel(int ms, {String today = 'Today', String yesterday = 'Yesterday'}) {
  final dt = dateTimeFromMs(ms);
  final now = DateTime.now();
  final date = DateTime(dt.year, dt.month, dt.day);
  final todayDate = DateTime(now.year, now.month, now.day);
  if (date == todayDate) return today;
  if (date == todayDate.subtract(const Duration(days: 1))) return yesterday;
  return formatDate(ms);
}

int estimatedMinutes(int chars) {
  if (chars <= 0) return 0;
  return (chars / 60).ceil();
}

String formatCount(int value) {
  if (value >= 100000000) {
    return '${(value / 100000000).toStringAsFixed(1)}亿';
  }
  if (value >= 10000) {
    return '${(value / 10000).toStringAsFixed(1)}万';
  }
  return value.toString();
}

String formatProgress(int received, int total) {
  if (total <= 0) return '';
  final percent = (received * 100 / total).clamp(0, 100).toStringAsFixed(0);
  return '$percent%';
}

String shorten(String text, int max) {
  if (text.length <= max) return text;
  return '${text.substring(0, max)}…';
}

String hotkeyLabel(int vk, int mods, String Function(String) tr) {
  final parts = <String>[];
  if (mods & 1 != 0) parts.add('Ctrl');
  if (mods & 2 != 0) parts.add('Alt');
  if (mods & 4 != 0) parts.add('Shift');
  if (mods & 8 != 0) parts.add('Win');
  parts.add(_vkName(vk));
  return parts.join(' + ');
}

String _vkName(int vk) {
  if (vk >= 0x70 && vk <= 0x87) return 'F${vk - 0x6F}';
  if (vk >= 0x30 && vk <= 0x39) return String.fromCharCode(vk);
  if (vk >= 0x41 && vk <= 0x5A) return String.fromCharCode(vk);
  const names = <int, String>{
    0x08: 'Backspace',
    0x09: 'Tab',
    0x0D: 'Enter',
    0x10: 'Shift',
    0x11: 'Ctrl',
    0x12: 'Alt',
    0x13: 'Pause',
    0x14: 'CapsLock',
    0x1B: 'Esc',
    0x20: 'Space',
    0x21: 'PageUp',
    0x22: 'PageDown',
    0x23: 'End',
    0x24: 'Home',
    0x25: 'Left',
    0x26: 'Up',
    0x27: 'Right',
    0x28: 'Down',
    0x2D: 'Insert',
    0x2E: 'Delete',
    0x5B: 'LeftWin',
    0x5C: 'RightWin',
    0x5D: 'Menu',
    0xA0: 'LeftShift',
    0xA1: 'RightShift',
    0xA2: 'LeftCtrl',
    0xA3: 'RightCtrl',
    0xA4: 'LeftAlt',
    0xA5: 'RightAlt',
    0xBA: ';',
    0xBB: '=',
    0xBC: ',',
    0xBD: '-',
    0xBE: '.',
    0xBF: '/',
    0xC0: '`',
    0xDB: '[',
    0xDC: '\\',
    0xDD: ']',
    0xDE: "'",
  };
  return names[vk] ?? 'VK${vk.toRadixString(16).toUpperCase().padLeft(2, '0')}';
}

String modeLabel(String? mode, String Function(String) tr) {
  switch ((mode ?? 'paste').toLowerCase()) {
    case 'type':
      return tr('history.mode.type');
    case 'oncall':
      return tr('history.mode.oncall');
    default:
      return tr('history.mode.paste');
  }
}

Color statusColor(String? status, bool listening) {
  if (status == 'ready') {
    return listening ? const Color(0xFF38E1D6) : const Color(0xFF45D483);
  }
  if (status == 'error') return const Color(0xFFFF5C66);
  return const Color(0xFFFFB020);
}
