# 图片与生成资源

角色为“DeepSeek 娘 / 小鲸”社区角色相关创作，非官方联名。

- 原始六状态图集：`web/assets/imagegen/whale-chan-atlas-v1.png`。
- 额外表情 PNG：`web/assets/imagegen/reactions-v1/`。
- 提示词与生成记录：同目录 `prompt.txt`、`prompts.json`、`asset-info.json`。
- 参考来源记录：[角色页面](https://aigengtu.com/DeepSeek%E5%A8%98)、[参考页面](https://aigengtu.com/zh/meme/303)。第三方参考照片不随仓库分发。
- 图片通过内置 image_gen 辅助生成；后处理包括固定单元切片、透明 WebP 编码、缩放与 LCD RGB565 调色板编码。

生成头文件已经提交，方便直接构建；网页运行时使用固件内图片，不访问上述网站。代码 MIT 许可不构成对第三方角色、名称或参考素材的额外授权。
