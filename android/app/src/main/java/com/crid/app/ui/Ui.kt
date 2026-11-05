package com.crid.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.crid.app.ui.theme.CridGreen
import com.crid.app.ui.theme.CridGrey
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

fun protocolName(p: Int): String = when (p) {
    4 -> "GB46750"
    3 -> "GB42590"
    else -> "ASTM"
}

fun protocolColor(p: Int): Color = when (p) {
    4 -> Color(0xFF5B21B6)
    3 -> Color(0xFF0369A1)
    else -> Color(0xFF6B7280)
}

fun fmt1(v: Double): String = String.format(Locale.US, "%.1f", v)
fun fmt2(v: Double): String = String.format(Locale.US, "%.2f", v)
fun fmt6(v: Double): String = String.format(Locale.US, "%.6f", v)

fun ageText(ms: Long): String = when {
    ms < 1000 -> "${ms}ms"
    ms < 60_000 -> "${ms / 1000}s"
    ms < 3_600_000 -> "${ms / 60_000}m"
    else -> "${ms / 3_600_000}h"
}

fun unixTimeText(sec: Long): String {
    if (sec <= 0) return "--"
    return SimpleDateFormat("MM-dd HH:mm:ss", Locale.getDefault()).format(Date(sec * 1000))
}

fun clockText(sec: Long): String {
    if (sec <= 0) return "--:--:--"
    return SimpleDateFormat("HH:mm:ss", Locale.getDefault()).format(Date(sec * 1000))
}

@Composable
fun SectionCard(
    title: String,
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit,
) {
    Card(
        modifier = modifier.fillMaxWidth(),
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(containerColor = Color.White),
        elevation = CardDefaults.cardElevation(defaultElevation = 1.dp),
    ) {
        Column(Modifier.padding(14.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(
                    Modifier
                        .size(width = 4.dp, height = 14.dp)
                        .background(CridGreen, RoundedCornerShape(2.dp))
                ) {}
                Text(
                    title,
                    modifier = Modifier.padding(start = 7.dp),
                    fontSize = 15.sp,
                    fontWeight = FontWeight.Bold,
                    color = com.crid.app.ui.theme.CridDark,
                )
            }
            content()
        }
    }
}

@Composable
fun Kv(label: String, value: String, highlight: Boolean = false) {
    Column(Modifier.padding(vertical = 3.dp)) {
        Text(value, fontSize = 13.sp, fontWeight = FontWeight.Bold, color = if (highlight) CridGreen else Color(0xFF111827))
        Text(label, fontSize = 10.sp, color = CridGrey)
    }
}

@Composable
fun StatChip(
    value: String,
    label: String,
    color: Color = com.crid.app.ui.theme.CridDark,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier
            .background(Color(0xFFF6F8FA), RoundedCornerShape(10.dp))
            .padding(horizontal = 10.dp, vertical = 7.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text(value, fontSize = 17.sp, fontWeight = FontWeight.Bold, color = color)
        Text(label, fontSize = 10.sp, color = CridGrey)
    }
}

@Composable
fun SegRow(
    options: List<Pair<Int, String>>,
    selected: Int,
    onChange: (Int) -> Unit,
) {
    Row(
        Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        options.forEach { (value, label) ->
            FilterChip(
                selected = selected == value,
                onClick = { onChange(value) },
                label = { Text(label, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis) },
                modifier = Modifier.weight(1f),
            )
        }
    }
}

@Composable
fun SelectRow(
    label: String,
    options: List<Pair<Int, String>>,
    selected: Int,
    onChange: (Int) -> Unit,
) {
    var open by remember { mutableStateOf(false) }
    val text = options.firstOrNull { it.first == selected }?.second ?: "-- 请选择 --"
    Column(Modifier.fillMaxWidth()) {
        Text(label, fontSize = 11.sp, color = CridGrey, fontWeight = FontWeight.SemiBold)
        OutlinedButton(
            onClick = { open = true },
            // 高度/内边距跟 OutlinedTextField 对齐（都是 56dp + 上方 3~4dp 间距），
            // 这样下拉框与它左边的输入框并排时上下边框才在同一条线上。
            // 左右内边距收窄到 12dp，长选项名不至于一上来就被挤掉。
            contentPadding = PaddingValues(horizontal = 12.dp, vertical = 0.dp),
            modifier = Modifier
                .fillMaxWidth()
                .padding(top = 3.dp)
                .height(56.dp),
        ) {
            Text(
                text,
                Modifier.weight(1f),
                fontSize = 14.sp,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            Text("▾", fontSize = 12.sp)
        }
    }
    if (open) {
        AlertDialog(
            onDismissRequest = { open = false },
            confirmButton = {
                TextButton(onClick = { open = false }) { Text("取消") }
            },
            title = { Text(label) },
            text = {
                Column {
                    options.forEach { (value, name) ->
                        TextButton(
                            onClick = {
                                onChange(value)
                                open = false
                            },
                            modifier = Modifier.fillMaxWidth(),
                        ) {
                            Text(
                                name,
                                Modifier.weight(1f),
                                fontSize = 14.sp,
                                color = if (value == selected) CridGreen else Color(0xFF1F2937),
                            )
                            if (value == selected) Text("✓", color = CridGreen)
                        }
                    }
                }
            },
        )
    }
}

@Composable
fun LabeledField(
    label: String,
    value: String,
    onValueChange: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(modifier.fillMaxWidth()) {
        Text(label, fontSize = 11.sp, color = CridGrey, fontWeight = FontWeight.SemiBold)
        OutlinedTextField(
            value = value,
            onValueChange = onValueChange,
            singleLine = true,
            textStyle = MaterialTheme.typography.bodyMedium,
            modifier = Modifier
                .fillMaxWidth()
                .padding(top = 3.dp),
        )
    }
}

@Composable
fun SwitchRow(title: String, checked: Boolean, onChange: (Boolean) -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(title, Modifier.weight(1f), fontSize = 14.sp)
        Switch(checked = checked, onCheckedChange = onChange)
    }
}
