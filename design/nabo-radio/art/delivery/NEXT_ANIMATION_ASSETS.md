# 后续动画资源清单
当前0条源视频、0动作完成。以下为目标，不是已交付帧。
12fps、256x256 RGBA、锚点(128,224)、每动作64色。
10必需68帧；2推荐12帧；1可选8帧，合计13动作88帧。

|原视频名|起始/变化|目标帧|循环|优先级|状态|
|---|---|---|---|---|---|
|idle_off.mp4|S1|8|是|required|待源视频|
|phones_on.mp4|S1->S2|6|否|required|待源视频|
|phones_off.mp4|S2->S1|6|否|required|待源视频|
|tune.mp4|S2|6|是|required|待源视频|
|groove.mp4|S2|8|是|required|待源视频|
|listen.mp4|S2|8|是|required|待源视频|
|sip.mp4|S2|8|否|required|待源视频|
|love.mp4|S2|5|否|required|待源视频|
|oops.mp4|S2|5|否|required|待源视频|
|doze.mp4|S2|8|是|required|待源视频|
|wave.mp4|S1|6|否|recommended|待源视频|
|search.mp4|S1|6|是|recommended|待源视频|
|sing.mp4|S2|8|是|optional|待源视频|

以design/S1_off_air.png或S2_on_air.png作起始图，不用带UI预览。
约5-6秒源视频交回art/video/batch1/source，原样保存不裁不转码；多次生成加_v2/_v3，PICKS.md记选取。
选一个完整动作/循环处理成预算帧，不将多次循环粗暴压进短段。先检查脸/眼镜/帽标、手部与镜头/桌沿稳定，再检查流畅和接缝。
S1系接idle_off；S2系接listen/groove，两者第0帧均需S2相同姿势。
sip须复用props/mug.png，戴摘耳机须B珊瑚色。静态双手藏桌后不等于动态手部已验收。
本交付未执行外部生成。
