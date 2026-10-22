import 'package:flutter/material.dart';

class SwColors {
  static const bg = Color(0xFF0E0E12);
  static const surface = Color(0xFF1A1A22);
  static const surface2 = Color(0xFF24242E);
  static const text = Color(0xFFECECF1);
  static const textMuted = Color(0xFF8A8A99);
  static const accentA = Color(0xFF7C5CFF);
  static const accentB = Color(0xFF38E1D6);
  static const success = Color(0xFF45D483);
  static const warn = Color(0xFFFFB020);
  static const error = Color(0xFFFF5C66);
  static const hairline = Color(0x14FFFFFF);
  static const hairlineStrong = Color(0x24FFFFFF);
  static const accentSoft = Color(0x297C5CFF);

  static const lightBg = Color(0xFFF6F6F8);
  static const lightSurface = Color(0xFFFFFFFF);
  static const lightSurface2 = Color(0xFFEFEFF3);
  static const lightText = Color(0xFF1A1A22);
  static const lightMuted = Color(0xFF71717F);
  static const lightHairline = Color(0x14000000);
  static const lightHairlineStrong = Color(0x1F000000);
}

class SwTheme {
  static const accentGradient = LinearGradient(
    begin: Alignment.topLeft,
    end: Alignment.bottomRight,
    colors: [SwColors.accentA, SwColors.accentB],
  );

  static ThemeData dark() => _build(Brightness.dark);
  static ThemeData light() => _build(Brightness.light);

  static ThemeData _build(Brightness brightness) {
    final dark = brightness == Brightness.dark;
    final bg = dark ? SwColors.bg : SwColors.lightBg;
    final surface = dark ? SwColors.surface : SwColors.lightSurface;
    final surface2 = dark ? SwColors.surface2 : SwColors.lightSurface2;
    final text = dark ? SwColors.text : SwColors.lightText;
    final muted = dark ? SwColors.textMuted : SwColors.lightMuted;
    final hairline = dark ? SwColors.hairline : SwColors.lightHairline;

    final scheme = ColorScheme(
      brightness: brightness,
      primary: SwColors.accentA,
      onPrimary: Colors.white,
      secondary: SwColors.accentB,
      onSecondary: Colors.black,
      error: SwColors.error,
      onError: Colors.white,
      surface: surface,
      onSurface: text,
      surfaceContainerHighest: surface2,
      outline: hairline,
      outlineVariant: hairline,
    );

    final base = ThemeData(
      useMaterial3: true,
      brightness: brightness,
      colorScheme: scheme,
      fontFamily: 'Segoe UI',
      scaffoldBackgroundColor: bg,
    );

    return base.copyWith(
      textTheme: base.textTheme
          .apply(bodyColor: text, displayColor: text)
          .copyWith(
            bodySmall: TextStyle(fontSize: 12, color: muted, height: 1.45),
            bodyMedium: TextStyle(fontSize: 13, color: text, height: 1.45),
            titleMedium: TextStyle(
              fontSize: 15,
              fontWeight: FontWeight.w600,
              color: text,
            ),
            titleLarge: TextStyle(
              fontSize: 20,
              fontWeight: FontWeight.w700,
              color: text,
              letterSpacing: -0.2,
            ),
          ),
      cardTheme: CardThemeData(
        color: surface,
        elevation: 0,
        margin: EdgeInsets.zero,
        shape: RoundedRectangleBorder(
          borderRadius: BorderRadius.circular(14),
          side: BorderSide(color: hairline),
        ),
      ),
      dividerTheme: DividerThemeData(color: hairline, thickness: 1, space: 1),
      filledButtonTheme: FilledButtonThemeData(
        style: FilledButton.styleFrom(
          backgroundColor: SwColors.accentA,
          foregroundColor: Colors.white,
          textStyle: const TextStyle(fontWeight: FontWeight.w600, fontSize: 13),
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(9),
          ),
          padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
        ),
      ),
      textButtonTheme: TextButtonThemeData(
        style: TextButton.styleFrom(
          foregroundColor: muted,
          textStyle: const TextStyle(fontWeight: FontWeight.w500, fontSize: 13),
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(8),
          ),
        ),
      ),
      outlinedButtonTheme: OutlinedButtonThemeData(
        style: OutlinedButton.styleFrom(
          foregroundColor: text,
          side: BorderSide(color: hairline),
          textStyle: const TextStyle(fontWeight: FontWeight.w500, fontSize: 13),
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(9),
          ),
          padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 11),
        ),
      ),
      inputDecorationTheme: InputDecorationTheme(
        filled: true,
        fillColor: surface2,
        contentPadding:
            const EdgeInsets.symmetric(horizontal: 12, vertical: 12),
        hintStyle: TextStyle(color: muted, fontSize: 13),
        border: OutlineInputBorder(
          borderRadius: BorderRadius.circular(9),
          borderSide: BorderSide(color: hairline),
        ),
        enabledBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(9),
          borderSide: BorderSide(color: hairline),
        ),
        focusedBorder: OutlineInputBorder(
          borderRadius: BorderRadius.circular(9),
          borderSide: const BorderSide(color: SwColors.accentA),
        ),
      ),
      dropdownMenuTheme: DropdownMenuThemeData(
        textStyle: TextStyle(color: text, fontSize: 13),
        menuStyle: MenuStyle(
          backgroundColor: WidgetStatePropertyAll(surface),
          shape: WidgetStatePropertyAll(
            RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
          ),
        ),
      ),
      tooltipTheme: TooltipThemeData(
        decoration: BoxDecoration(
          color: surface2,
          borderRadius: BorderRadius.circular(8),
          border: Border.all(color: hairline),
        ),
        textStyle: TextStyle(color: text, fontSize: 12),
      ),
      snackBarTheme: SnackBarThemeData(
        backgroundColor: surface2,
        contentTextStyle: TextStyle(color: text, fontSize: 13),
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
        behavior: SnackBarBehavior.floating,
      ),
    );
  }
}
