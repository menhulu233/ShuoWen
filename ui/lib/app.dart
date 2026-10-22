import 'package:flutter/material.dart';

import 'core/engine_client.dart';
import 'core/l10n.dart';
import 'core/theme.dart';
import 'widgets/common.dart';
import 'windows/history_window.dart';
import 'windows/hud_window.dart';
import 'windows/oncall_window.dart';
import 'windows/popup_window.dart';
import 'windows/settings_window.dart';

class ShuoWenApp extends StatelessWidget {
  const ShuoWenApp({
    super.key,
    required this.client,
    required this.windowType,
  });

  final EngineClient client;
  final String windowType;

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: client,
      builder: (context, _) {
        final l10n = L10n.fromSetting(client.language);
        return L10nScope(
          l10n: l10n,
          child: MaterialApp(
            debugShowCheckedModeBanner: false,
            title: '说文 ShuoWen',
            theme: SwTheme.dark(),
            darkTheme: SwTheme.dark(),
            themeMode: ThemeMode.dark,
            locale: Locale(l10n.code),
            home: SwToastHost(
              client: client,
              child: _windowFor(client, windowType),
            ),
          ),
        );
      },
    );
  }

  Widget _windowFor(EngineClient client, String windowType) {
    switch (windowType) {
      case 'hud':
        return HudWindow(client: client);
      case 'oncall':
        return OnCallWindow(client: client);
      case 'popup':
        return PopupWindow(client: client);
      case 'history':
        return HistoryWindow(client: client);
      default:
        return SettingsWindow(client: client);
    }
  }
}
