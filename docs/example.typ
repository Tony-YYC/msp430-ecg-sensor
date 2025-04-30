#set page(
  paper: "a4",
  margin: (x: 2.5cm, y: 2.5cm),
)

#set text(
  font: (
    "Times New Roman",
    "SimSun"
  )
)

#let heading-font = ("Times New Roman","SimHei")

// 为特定语言设置字体
#set text(lang: "zh", font: "SimSun")
#set text(lang: "en", font: "Times New Roman")

#set heading(numbering: "1.")
#show heading: set text(font: heading-font, weight: "bold")

#set par(
    leading: 10pt,
    justify: true,
    first-line-indent: 2em,
)

#align(center)[
  #text(font: heading-font, size: 16pt, weight: "bold")[
    标题
  ]
]

= 一级标题

已经实现中文用宋体（Simsun）,and English use Times New Roman

+ 数字列表格式示例
+ 数字列表格式示例
+ 数字列表格式示例

- 点列表格式示例
  - 点列表格式示例
    - 点列表格式示例
- 点列表格式示例
- 点列表格式示例

*粗体示例*
_斜体示例_（SimSun疑似没有斜体支持）

== 二级标题

=== 三级标题

= 一级标题

= 一级标题

= 一级标题