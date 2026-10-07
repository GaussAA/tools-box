#pragma once

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QString>

#include <functional>

// 数字水印插件的「嵌入 / 提取」纯逻辑。
//
// ── 为什么是 DCT 而不是 LSB ──────────────────────────────────────────────────
// 直觉上「改最低位」最简单，但图片一旦存成 JPEG，量化表会把 LSB 直接抹平，
// 存活率约等于 0；PNG 虽然无损，可用户转手存成 JPEG 就全丢了。所以本实现走
// JPEG 领域的成熟路线：把图拆成 8×8 块做二维 DCT，**只动中频系数**。
//
//   * 低频（DC 分量及其邻近）决定画面的整体明暗与色调，动它 = 图片变色；
//   * 高频承载噪点与细节，动它 = 一压就丢，JPEG 里最先被量化掉。
//   * 中频是唯一「人眼不敏感、量化又保留」的区域，水印存活的关键就在这里。
//
// ── 单个比特怎么塞 ───────────────────────────────────────────────────────────
// 用一对**对称位置**的系数 (u,v) 与 (v,u) 承载一个比特，取两者的**差值** D：
//
//     比特 1 → D ≥  +strength      比特 0 → D ≤  -strength
//
// 这样做的三个好处：
//   1. JPEG 量化对两处系数的影响高度相似，差值被抵消掉大部分，存活率显著提高；
//   2. 直流分量（整体偏亮偏暗）对两处影响相同，差值里自动被消掉 —— 所以不必
//      事先知道原始系数是多少；
//   3. 提取时只需看差值的符号，不需要原始图像，实现是**盲检测**。
//
// ── 冗余：同一份载荷在多条「块带」里各写一遍 ────────────────────────────────
// 有损压缩与裁剪会打掉一部分块。整图分成若干条横向块带，每条都完整写一遍载荷；
// 提取时逐带校验（CRC32），任一条通过即可还原。被破坏的带会自然被 CRC 淘汰。
//
// 本文件不得 include 任何 QtWidgets 头文件。

namespace imgwatermark {

/// 8×8 分块边长。DCT 的基本单元，改这个值要连带改系数表与配对表。
inline constexpr int kBlockSize = 8;

/// 每块承载的比特数 = 参与调制的系数对数量。
///
/// **取 1**（每块只用一对中频系数）。这是实测结论，不是省事：一个 8×8 块里同时调制
/// 8 对时，各对基函数在同一像素上取值不同甚至反号，合成后的**最大像素改动**达
/// 60–70 级（肉眼清晰可见的块状纹理）；只用 1 对时，同等比特错误率（0%）下最大
/// 像素改动约 33 级，且强度从 16 翻到 32 也只涨到 36 —— 也就是说**像素改动主要由
/// 「块内调制多少对」决定，而非强度大小**。容量靠更多块来撑，不靠块内更多对。
/// 完整实测数据见 docs/error_ledger.md 第 11、12 条。
///
/// 数值**必须与 core/Stego.cpp 里的kCoefficientPairs 表长一致** —— 那里用
/// `std::size()` 在编译期断言了这一点（见 kBitsPerBlockMatchesPairTable）。
/// 两处手工同步过一次，失配时容量计算会整条错掉且不报编译错误
/// （见 docs/error_ledger.md 第 12 条），所以改成由编译器兜住。
inline constexpr int kBitsPerBlock = 1;

/// 冗余块带数上限。带越多抗破坏越强，但每条带能装的载荷越少（二者此消彼长）。
inline constexpr int kMaxStripes = 6;

/// 像素域幅度 → 系数域幅度的换算系数。
///
/// 实测（每块一对中频系数 + 像素域闭环补偿，见 docs/error_ledger.md 第 12、14 条）：
///   系数幅度 10 → 152 比特错 1 位（提取失败）
///   系数幅度 12 → 零错误，最大像素改动 40
///   系数幅度 24 → 零错误，最大像素改动 43
/// 两个要点：① 零错误阈值约在系数幅度 12；② **最大像素改动几乎不随强度变化**
/// （12→24 只涨 3 级）—— 改动量由「每块一对」这个结构决定，不是强度。
/// 故取 1.0：用户设定的像素强度直接作为系数幅度，默认 16 稳在零错误区间。
inline constexpr double kPixelToCoefficientRatio = 1.0;
/// 嵌入强度默认值（**像素域**灰阶幅度，0–255 量纲）。
///
/// 强度刻意用像素域而非系数域来表达，有两个理由：
///   1. **它才是用户能理解的量**：「强度 16」意为「每像素亮度最多动 16 级」，
///      而系数域的数字对用户毫无意义；
///   2. **「不可见」是硬需求**：实测默认强度 16 下最大像素改动约 41 级
///      （255 量纲的 16%，肉眼不刻意对比即看不出），且该值对强度不敏感
///      （强度从 12 翻到 24 只涨 3 级）—— 这条由 tests 的 embedStaysInvisible() 守住。
inline constexpr double kDefaultStrength = 16.0;

/// 嵌入强度下限。实测系数幅度 10 就会错1 比特（提取失败）、12 才零错误，
/// 所以下限取 12 —— 让用户无论怎么拖滑块，拿到的都是**能提取**的结果，
/// 而不是「成功嵌入了但提不出来」。
inline constexpr double kMinStrength = 12.0;

/// 嵌入强度上限。再大则人眼明显看出纹理变化，违背「不可见」这一前提。
inline constexpr double kMaxStrength = 32.0;

/// 嵌入 / 提取的进度回调。参数是 0–100 的完成百分比。
///
/// 嵌入是 CPU 密集型（一张 4000×3000 要做十几万次二维 DCT），放后台线程执行，
/// 界面靠它报进度 —— 没有它，用户只会看到界面卡住（见 docs/architecture.md §5）。
using ProgressCallback = std::function<void(int)>;

/// 嵌入结果。
struct EmbedResult
{
    bool ok = false;         ///< 是否成功
    QImage image;            ///< 成功时为已嵌水印的图像（原图不被修改）
    QString errorText;       ///< 失败原因，可直接展示给用户
    int capacityBytes = 0;   ///< 本图在当前参数下的总容量（字节）
    int usedBytes = 0;       ///< 实际写入的字节数（含载荷头）
    int blocks = 0;          ///< 参与写入的 8×8 块总数
    int stripes = 0;         ///< 冗余块带数，1 表示未启用冗余
};

/// 提取结果。
struct ExtractResult
{
    bool ok = false;             ///< 是否成功解出
    QByteArray payload;          ///< 成功时为水印正文（未含载荷头）
    QString errorText;           ///< 失败原因
    int recoveredStripes = 0;    ///< 通过 CRC 校验的块带数
    int totalStripes = 0;        ///< 尝试过的块带数
};

/// 嵌入后的图相对原图的**最大单通道像素改动**（0–255 量纲）。
///
/// 这是「水印肉眼不可见」这一定性需求的**定量守卫**。人眼在正常观看距离上分辨不出
/// 约 1%–2% 的亮度变化，即 255量纲上的 2–5 级；返回值超过这个量级就说明强度换算
/// 或基函数增益估错了 —— 而这类错误不会让任何「提取成功」的断言失败，只会让工具
/// 悄悄交付一个「能跑但一眼看得出被动过」的产物。
///
/// @param source 原图。
/// @param embedded embedWatermark 的产物。
/// @return 最大绝对通道差；任一图为空时返回 0。
int maxPixelDelta(const QImage &source, const QImage &embedded);

/// 校验系数配对表自身是否合法（各对互不重叠、索引不越界、不碰直流）。
///
/// 这不是可有可无的自检：配对表一旦混进 (u,v) 与 (v,u)，两者会落到相同的两个
/// 下标上却被分配给**两个不同的比特**，嵌入与提取在同一处自相矛盾，症状是
/// 「每 8 比特固定错第 N 位 + CRC 失败」——从现象几乎推不回原因。
/// 把这张表的可断言性质暴露出来，就能让测试守住它。
///
/// @return 空串表示合法；否则返回违规描述。
QString validateCoefficientPairs();

/// 正变换→逆变换的**往返残差**（8×8 固定测试块的逐点最大偏差）。
///
/// 用途是给「正逆是否真的互逆」提供一把可直接断言的尺子。这个性质是整个算法的
/// 地基：归一化一旦写错（例如逆变换误用了正变换——正交矩阵 A满足 AᵀA = I，但
/// 并不等于 A = Aᵀ），块会被整体扭转，症状是「嵌入成功却提不出水印」，看起来
/// 像算法不work，实际上是一处归一化错误。有了这个残差，同类问题能被就地钉住。
///
/// @return 残差。正确实现下应在 1e-9 量级（纯浮点舍入）；数量级明显变大即说明
///         正逆变换不再互逆。
double dctRoundTripResidual();

/// 计算这张图能装多少字节的水印（不含载荷头开销）。
///
/// 供界面**事前**判断「内容是不是太长」，把「容量不足」这种失败从运行后提前到
/// 选完文件就提示，而不是让用户白等一次运算。
///
/// @param imageSize 图像像素尺寸。
/// @return 可用字节数；图小到一块都放不下时返回 0。
int capacityBytes(const QSize &imageSize);

/// 把载荷嵌入图像的中频系数。
///
/// 不修改 @p source（内部转为副本），返回新图。**建议输出为 PNG 或高质量 JPEG**：
/// 每次转存都会损耗水印，格式转换本身就是一次有损操作。
///
/// @param source  原图（可为空）。
/// @param payload 已由 buildPayload() 封装好的载荷字节。
/// @param strength 嵌入强度，取值范围 [kMinStrength, kMaxStrength]，越界会被夹取。
/// @param stripes 冗余块带数，取值范围 [1, kMaxStripes]，越界会被夹取。
/// @param progress 进度回调，可为空。
///
/// @note 冗余与容量此消彼长：条带越多，每条带能装的载荷越少。若指定条数装不下
///       本载荷，会自动回退到更大的可行条数；连无冗余都装不下才报错。
EmbedResult embedWatermark(const QImage &source, const QByteArray &payload, double strength,
                           int stripes = 1, const ProgressCallback &progress = ProgressCallback());

/// 从图像中提取水印载荷（盲检测，不需要原图）。
///
/// 内部对所有可能的块带条数逐一尝试，因此**不需要知道嵌入时用了多少条带**。
/// 这让提取端可以独立于嵌入端的参数 —— 用户换台机器、丢了配置也能解。
///
/// @param image 待检图像（通常是已保存的文件重新读入）。
/// @param progress 进度回调，可为空。
ExtractResult extractWatermark(const QImage &image,
                               const ProgressCallback &progress = ProgressCallback());

} // namespace imgwatermark
