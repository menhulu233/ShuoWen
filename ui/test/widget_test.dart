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
