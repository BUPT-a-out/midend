# midend

Check [.github/workflows/ci.yml](.github/workflows/ci.yml) for building and testing instructions.

## IR 生成约定

1. br / br cond 指令只能为基本块的最后一条指令
2. alloca 指令只能在函数的第一个基本块（entry）中使用