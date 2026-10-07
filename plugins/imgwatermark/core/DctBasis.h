#pragma once

#include "core/Stego.h"

#include <QImage>

#include <array>
#include <cmath>
#include <utility>

// 数字水印插件的「DCT 基底设施」：二维 DCT / IDCT、8×8 块的亮度读写、
// 以及把一个比特调制进一对中频系数。
//
// 从 Stego.cpp 拆出来的原因：那边除了算法流程，还得管载荷帧、容量估算、
// 冗余带与嵌入/提取编排，混在一起会超过 600 行的门禁（docs/architecture.md §9
// 偏差 9.7 已记录「插件 CMake 样板重复」的同类问题）。而这里的代码有一个
// 清晰的边界：**只做像素↔系数域的转换与调制，不关心水印的业务语义**。
//
// 定义放在匿名 namespace里：本文件只被 Stego.cpp 一个 TU 包含（单消费者），
// 这样内部链接的辅助函数不会外泄符号。若日后有第二个消费者，需改成 inline 函数
// 并去掉 namespace —— 由编译器兜住（否则每个 TU 各留一份副本，静默膨胀）。
//
// 本文件不得 include 任何 QtWidgets 头文件。

namespace imgwatermark {
namespace {

/// 参与调制的系数对 (u,v)，严格 u<v 且落在中频。
///
/// **每块只用一对**（kBitsPerBlock = 1），容量靠「更多块」而不是「块内更多对」
/// 来撑。这是实测逼出来的结论，不是省事：一个 8×8 块里同时调制 8 对系数时，
/// 各对对应的基函数在同一像素上取值不同甚至反号，合成后的**最大像素改动**
/// 达 60–70 级（肉眼清晰可见的块状纹理）；而只用 1 对时，同样的比特错误率下
/// 最大像素改动只有约 33 级，且强度翻倍也不显著上涨 —— 也就是说**「不可见」
/// 这条需求本质上要求块内改动量小**，而容量只是需要更多的块，不是更多的对。
/// 实测数据见 docs/error_ledger.md 第 11、12 条。
///
/// 取值范围 u,v ∈ [1,6]：避开 (0,*) 的直流行与 (*,0) 的直流列（那是亮度基准，
/// 动它整块色偏），也避开 7/7 一带（最先被 JPEG 量化掉）。
///
/// **必须严格 u<v 且各对之间不重叠**：配对 (u,v) 用到的下标是 v·8+u 与 u·8+v，
/// 于是 (u,v) 与 (v,u) 会落到**完全相同的两个下标**上，只是 A、B 互换。若把它们
/// 当成两对分给**两个不同的比特**，后写的会覆盖先写的，症状是「每块固定错一位 +
/// CRC 失败」。该表的可断言性质由 tests/tst_stego.cpp 的
/// coefficientPairsAreDisjoint() 守住。
///
/// 改这张表必须同步改 kBitsPerBlock（每块比特数 = 表长），否则容量计算与
/// 比特流宽度会不一致 —— capacityBytes() 与嵌入/提取两处都依赖它。
constexpr std::array<std::pair<int, int>, kBitsPerBlock> kCoefficientPairs = {{{1, 3}}};

/// 编译期兜住「配对表长度 == 每块比特数」这条不变量。
///
/// 两处曾手工同步，失配时报的是「未检测到有效水印」—— 与真实病因毫无关联，
/// 排查成本极高（见 docs/error_ledger.md 第 12 条）。改成 static_assert 后，
/// 改动配对表却忘了改 kBitsPerBlock，会在编译期立刻失败。
static_assert(kCoefficientPairs.size() == static_cast<std::size_t>(kBitsPerBlock),
              "配对表长度必须等于每块比特数 kBitsPerBlock");

/// DCT 基向量表：cos((2x+1) * u * π / 16)，u 与 x 均 ∈ [0,8)。
///
/// 二维 DCT 可分离成「先行后列」两趟一维变换，各 64 次乘加，比朴素二维的
/// 64×64 次少一个数量级 —— 一张 4000×3000 的图有约 18 万块，这个差距是秒级
/// 与十秒级的区别。
///
/// π 用字面量而不是 `M_PI`：后者**不是标准 C++**（由平台扩展提供，MSVC 还要求
/// 先定义 `_USE_MATH_DEFINES`），换个工具链就编译不过 —— 本项目 C++17、禁用扩展，
/// 正因为如此才需要它。
constexpr double kPi = 3.14159265358979323846;

const std::array<double, 64> &basisTable()
{
    static const std::array<double, 64> table = []() {
        std::array<double, 64> values = {};
        for (int u = 0; u < kBlockSize; ++u) {
            for (int x = 0; x < kBlockSize; ++x) {
                const double angle = (2.0 * x + 1.0) * u * kPi / (2.0 * kBlockSize);
                values[static_cast<std::size_t>(u * kBlockSize + x)] = std::cos(angle);
            }
        }
        return values;
    }();
    return table;
}

/// 正交归一化因子 alpha(u) = sqrt(c(u)/N)，c(0) = 1、c(u) = 2。
///
/// N = kBlockSize，所以 u=0 时是 sqrt(1/8)，其余是 sqrt(2/8)。
/// **这个因子必须精确**：写错会让「能量不守恒」（正交性失效），
/// 而症状同样是「嵌入成功却提不出来」，与配对/索引错误混在一起极难定位。
double normalization(int u)
{
    return (u == 0) ? std::sqrt(1.0 / kBlockSize) : std::sqrt(2.0 / kBlockSize);
}

/// 一维正变换（DCT-II）：out[u] = alpha(u) ·Σ_x in[x]·cos((2x+1)uπ/2N)。
void transformRow(const double *in, double *out)
{
    const auto &basis = basisTable();
    for (int u = 0; u < kBlockSize; ++u) {
        double sum = 0.0;
        for (int x = 0; x < kBlockSize; ++x) {
            sum += in[x] * basis[static_cast<std::size_t>(u * kBlockSize + x)];
        }
        out[u] = sum * normalization(u);
    }
}

/// 一维逆变换：正变换矩阵的转置 Aᵀ。
///
/// 归一化因子乘在**输入**下标（系数 u）上，而不是输出下标上——这正是 A 与 Aᵀ
/// 的唯一差别（A 正交只保证 AᵀA = I，**并不意味着 A = Aᵀ**：DCT-II 的基函数
/// cos((2x+1)uπ/16) 关于 x、u 并不对称）。核函数本身与正变换共用一张表，
/// 因为转置关系体现在「谁带 alpha」上，而不是表的内容上。
void transformRowInverse(const double *in, double *out)
{
    const auto &basis = basisTable();
    for (int x = 0; x < kBlockSize; ++x) {
        double sum = 0.0;
        for (int u = 0; u < kBlockSize; ++u) {
            sum += in[u] * normalization(u) * basis[static_cast<std::size_t>(u * kBlockSize + x)];
        }
        out[x] = sum;
    }
}

/// 二维 DCT：先行后列。
void forwardDct(const double in[64], double out[64])
{
    double rows[64] = {};
    double line[8] = {};
    double result[8] = {};

    // 第一趟：沿 x 方向做正变换 → rows[y][u]（行仍是空间行，列已是频率 u）。
    for (int y = 0; y < kBlockSize; ++y) {
        transformRow(&in[y * kBlockSize], line);
        for (int u = 0; u < kBlockSize; ++u) {
            rows[y * kBlockSize + u] = line[u];
        }
    }
    // 第二趟：沿 y 方向做正变换 → spec[v][u]。
    // 取数必须按步长 kBlockSize（固定 u、遍历 y）；写成 &rows[u] 是连续读，
    // 拿到的是相邻的 u 而非相邻的 y。
    for (int u = 0; u < kBlockSize; ++u) {
        for (int y = 0; y < kBlockSize; ++y) {
            line[y] = rows[y * kBlockSize + u];
        }
        transformRow(line, result);
        for (int v = 0; v < kBlockSize; ++v) {
            out[v * kBlockSize + u] = result[v];
        }
    }
}

/// 二维 IDCT：forwardDct 的逆运算。
///
/// 正向的存储约定是 spec[v][u]（v 为行、u 为列）。求逆要**倒着来**且
/// **按同一套频率下标配对**取数：先撤销第二趟（固定 u、遍历 v），再撤销第一趟
/// （固定 y、遍历 u）。
///
/// 这里最容易错在「两趟都用同一个遍历方向」上——那看起来对称，实则一行里
/// 取的是相邻 v，块被扭转，症状是「嵌入成功却提不出水印」。残差由
/// dctRoundTripResidual() 就地断言，见 tests/tst_stego.cpp。
void inverseDct(const double in[64], double out[64])
{
    double rows[64] = {};
    double line[8] = {};
    double result[8] = {};

    // 撤销第二趟：固定 u，沿 v 求逆 → rows[y][u]。
    for (int u = 0; u < kBlockSize; ++u) {
        for (int v = 0; v < kBlockSize; ++v) {
            line[v] = in[v * kBlockSize + u];
        }
        transformRowInverse(line, result);
        for (int y = 0; y < kBlockSize; ++y) {
            rows[y * kBlockSize + u] = result[y];
        }
    }
    // 撤销第一趟：固定 y，沿 u 求逆 → out[y][x]。
    for (int y = 0; y < kBlockSize; ++y) {
        transformRowInverse(&rows[y * kBlockSize], result);
        for (int x = 0; x < kBlockSize; ++x) {
            out[y * kBlockSize + x] = result[x];
        }
    }
}

/// 从图像取一个 8×8 块的亮度值。
///
/// 只取亮度（Y 分量）而不动色度：水印进了色度通道，人眼对色度差异远比亮度
/// 敏感，稍强一点就能看出来；进亮度则稳得多。
///
/// @param image 源图（须为 Format_Grayscale8 或能被 Qt 转换的格式）。
/// @param blockX 块列号（0 起）。
/// @param blockY 块行号（0 起）。
/// @param out 接收 64 个亮度值。
void readLumaBlock(const QImage &image, int blockX, int blockY, double out[64])
{
    // **这里必须走 constScanLine 而不是 image.pixel()**。
    //
    // 这不是风格偏好，是正确性问题：QImage::pixel() 即使在 const 对象上也会触发
    // detach（写时复制），把整个像素缓冲区深拷贝一份。在「读块 → 改系数 → 写块」
    // 的循环里每读一次就detach 一次，于是写回的不是 detach 之后那份数据，
    // 最终 `result.image = target` 交出去的那张图与实际写入的缓冲脱节。
    // 症状极具迷惑性：嵌入报告成功、像素改动非零（改到的是那份临时副本），
    // 而提取读到的是从未被写过的原始内容 —— 「嵌入永远成功、提取永远失败」。
    // constScanLine 只读不 detach，没有这个副作用。
    for (int y = 0; y < kBlockSize; ++y) {
        // 图像尺寸常不是 8 的倍数，边缘块要夹取坐标而不是越界读取。
        const int sy = std::min(blockY * kBlockSize + y, image.height() - 1);
        const uchar *scan = image.constScanLine(sy);
        for (int x = 0; x < kBlockSize; ++x) {
            const int sx = std::min(blockX * kBlockSize + x, image.width() - 1);
            // Format_RGB32 在小端下的**内存字节序是 B, G, R, A**，不是 R, G, B。
            // 按 R,G,B 读会**互换红蓝通道**，使 qGray 偏差达 0.1875·(B−R) 级
            // （R=10/B=30 时偏 3.75 级）——足以让「写入时算的亮度」与
            // 「读取时算的亮度」对不上，表现为提取全程解错。
            // 这个坑与 endianness 绑定，因此**换平台（大端）同样要改**，
            // 故这里显式写出字节序并加注释，而不是用 memcpy 之类的技巧掩盖。
            const QRgb pixel = qRgba(scan[sx * 4 + 2], scan[sx * 4 + 1], scan[sx * 4], 255);
            out[y * kBlockSize + x] = static_cast<double>(qGray(pixel));
        }
    }
}

/// 把 8×8 亮度块写回图像（**朴素色度平移**）。
///
/// 只做一件事：三通道同加 delta，使**读回的 qGray**尽量接近目标亮度。
/// 为什么不再做「精确命中亮度」的邻域搜索（曾有过，见 git 历史）：实测半径 3 时
/// **91.6% 的像素找不到精确解**，半径 32 仍有 62% 失败 —— 在 RGB 里凑一个能让
/// `(11r+16g+5b)/32` 整数除法恰好命中的颜色，本质上是逆一个带截断的方程，
/// 成功率极低。而**这一步的精确性其实并不重要**：真正要保证的是
/// 「写完之后重新读图像，系数差值仍有足够裕度」，那件事由嵌入主循环的
/// **回读校验**负责（见 embedBlockWithMargin）。把复杂度花在正确的地方。
void writeLumaBlock(QImage &image, int blockX, int blockY, const double block[64])
{
    for (int y = 0; y < kBlockSize; ++y) {
        const int sy = std::min(blockY * kBlockSize + y, image.height() - 1);
        // 用可写 scanLine 直取像素。这里确实要改图像，但**不要用 pixel()** ——
        // 它会 detach（见 readLumaBlock 的注释）。
        uchar *scan = image.scanLine(sy);
        for (int x = 0; x < kBlockSize; ++x) {
            const int sx = std::min(blockX * kBlockSize + x, image.width() - 1);
            const int value =
                qBound(0, static_cast<int>(std::lround(block[y * kBlockSize + x])), 255);
            // 字节序同 readLumaBlock：Format_RGB32 在小端下内存是 B, G, R, A。
            const QRgb pixel = qRgba(scan[sx * 4 + 2], scan[sx * 4 + 1], scan[sx * 4], 255);
            const int luma = qGray(pixel);
            const int delta = value - luma;
            if (delta == 0) {
                continue;
            }
            const int red = qBound(0, qRed(pixel) + delta, 255);
            const int green = qBound(0, qGreen(pixel) + delta, 255);
            const int blue = qBound(0, qBlue(pixel) + delta, 255);
            // 写回按 B, G, R, A 的字节序（见 readLumaBlock 的注释）。
            scan[sx * 4] = static_cast<uchar>(blue);
            scan[sx * 4 + 1] = static_cast<uchar>(green);
            scan[sx * 4 + 2] = static_cast<uchar>(red);
            scan[sx * 4 + 3] = static_cast<uchar>(qAlpha(pixel));
        }
    }
}

/// 按给定比特序列调制一对系数，使其差值带上正/负号。
///
/// @param block 系数块（原地修改）。
/// @param pairIndex kCoefficientPairs 的下标。
/// @param bit  true → 差值为正，false → 差值为负。
/// @param strength 推开的幅度。
void modulatePair(double block[64], int pairIndex, bool bit, double strength)
{
    const auto [u, v] = kCoefficientPairs[static_cast<std::size_t>(pairIndex)];
    const int indexA = v * kBlockSize + u;
    const int indexB = u * kBlockSize + v;

    // 关键：只调整**两者之差**。若差值已满足目标符号则不动，否则对称地把两者
    // 往目标方向各推一半。这样处理保持 (u,v) 与 (v,u) 的和不变，也就保持了该
    // 块的平均亮度不变 —— 不会出现「改了水印导致整块变亮/变暗」。
    const double difference = block[indexA] - block[indexB];
    const double target = bit ? strength : -strength;
    if ((bit && difference >= target) || (!bit && difference <= target)) {
        return;
    }
    const double adjust = (target - difference) * 0.5;
    block[indexA] += adjust;
    block[indexB] -= adjust;
}

/// 读取一对系数差值的符号，即还原出比特。
///
/// @return true 表示差值为正（比特 1）。
bool demodulatePair(const double block[64], int pairIndex)
{
    const auto [u, v] = kCoefficientPairs[static_cast<std::size_t>(pairIndex)];
    return (block[v * kBlockSize + u] - block[u * kBlockSize + v]) > 0.0;
}
} // namespace

} // namespace imgwatermark
