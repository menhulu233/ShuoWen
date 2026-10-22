import 'package:flutter_test/flutter_test.dart';
import 'package:shuowen_ui/core/format.dart';
import 'package:shuowen_ui/core/l10n.dart';

void main() {
  test('localization falls back to English', () {
    const english = L10n('en');
    const chinese = L10n('zh');
    expect(english.t('settings.title'), 'Settings');
    expect(chinese.t('settings.title'), '设置');
    expect(english.t('missing.key'), 'missing.key');
  });

  test('every locale defines every key', () {
    final allKeys = L10n.allKeys.toList()..sort();
    expect(allKeys, isNotEmpty);
    for (final code in ['en', 'zh', 'ja', 'ko']) {
      final missing = allKeys
          .where((key) => !L10n(code).keys.contains(key))
          .toList();
      expect(
        missing,
        isEmpty,
        reason:
            '$code is missing ${missing.length} keys: ${missing.join(', ')}',
      );
    }
  });

  test('every locale preserves placeholders', () {
    final placeholders = RegExp(r'\{[a-zA-Z]+\}');
    Set<String> vars(String value) =>
        placeholders.allMatches(value).map((match) => match[0]!).toSet();
    final english = L10n('en');
    for (final code in ['zh', 'ja', 'ko']) {
      final locale = L10n(code);
      for (final key in english.keys) {
        expect(
          vars(locale.t(key)),
          vars(english.t(key)),
          reason: '$code placeholder mismatch for $key',
        );
      }
    }
  });

  test('hotkey labels include modifiers', () {
    expect(hotkeyLabel(0xA3, 0, (key) => key), 'RightCtrl');
    expect(hotkeyLabel(0x41, 3, (key) => key), 'Ctrl + Alt + A');
  });

  test('estimated typing time', () {
    expect(estimatedMinutes(0), 0);
    expect(estimatedMinutes(60), 1);
    expect(estimatedMinutes(61), 2);
  });
}
