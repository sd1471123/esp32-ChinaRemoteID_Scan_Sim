package com.crid.app.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

val CridGreen = Color(0xFF0F8A4D)
val CridDark = Color(0xFF0B3D2E)
val CridGreenLight = Color(0xFFE8F5EE)
val CridOrange = Color(0xFFE67E22)
val CridRed = Color(0xFFC0392B)
val CridGrey = Color(0xFF8B95A1)

private val scheme = lightColorScheme(
    primary = CridGreen,
    onPrimary = Color.White,
    primaryContainer = CridGreenLight,
    onPrimaryContainer = CridDark,
    secondary = CridDark,
    onSecondary = Color.White,
    background = Color(0xFFEEF1F5),
    onBackground = Color(0xFF1F2937),
    surface = Color.White,
    onSurface = Color(0xFF1F2937),
    surfaceVariant = Color(0xFFF6F8FA),
    onSurfaceVariant = Color(0xFF6B7280),
    error = CridRed,
)

@Composable
fun CridTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = scheme, content = content)
}
