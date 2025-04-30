#set page(
  paper: "a4",
  margin: (x: 2.5cm, y: 2.5cm),
)

#import "@preview/cuti:0.3.0": show-cn-fakebold
#show: show-cn-fakebold

#let heading-font = ("Times New Roman","SimHei")
#let body-font = ("Times New Roman","SimSun")

#set text(font: body-font, lang: "zh") // 设置正文默认字体和语言

#set heading(numbering: "1.")
#show heading: set text(font: heading-font, weight: "bold")

#set par(
    leading: 10pt,
    justify: true,
    first-line-indent: 2em,
)

#align(center)[
  #text(font: heading-font, size: 20pt, weight: "bold")[
    实验3报告：B 超图像伪影与气泡对成像的影响
  ]
]

= 大作业的名称

= 组员组成以及各自分工

= 项目的系统框图

= 项目用到的MSP430的主要硬件和外设资源

== MSP430片内外设：ADC

== MSP430片内外设：点阵屏驱动

== MSP430片内外设：硬件UART

== 模拟心电前端：AD8232

== 蓝牙模块选型:HC05/HC10/ESP32

选型暂未完全确定

= 预期实现的功能以及可展示的结果

== 使用MSP430片内ADC采集心电

== 在彩色点阵TFT LCD上显示采集到的图像

== 使用UART传输至上位机，并编写上位机程序绘制图像

== (待定)使用蓝牙无线传输至手机，并编写移动端安卓应用绘制结果

= 项目中可能遇到的风险与困难