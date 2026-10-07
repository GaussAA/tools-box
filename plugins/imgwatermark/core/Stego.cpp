#include "core/Stego.h"

#include "core/DctBasis.h"
#include "core/StegoPayload.h"

#include <QSet>
#include <QThread>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace imgwatermark {
namespace {

/// 回读校验的最大重试轮数（每轮把系数幅度加倍）。
///
/// 多数块第一轮就能达标；少数「像素量化吃掉太多幅度」的块需要加大幅度。
/// 6 轮意味着最多放大 32 倍，对应系数幅度 16 → 512，超过它再试也只是把图
/// 改得面目全非，故就此打止并接受失败。
constexpr int kEmbedVerifyMaxRounds = 6;

/// 回读校验要求的差值裕度（系数域单位）。
///
/// 实测：系数幅度 12 时像素往返后的实际差值约 6–11，故取 6 作为「稳」的门槛。
/// 低于它提取时的符号判断会开始出现翻转。
constexpr double kEmbedTargetMargin = 6.0;

/// 整幅图的块网格尺寸。
std::pair<int, int> blockGrid(const QSize &size)
{
    return {(size.width() + kBlockSize - 1) / kBlockSize,
            (size.height() + kBlockSize - 1) / kBlockSize};
}

/// 计算给定块带数下的单带容量（字节）。
///
/// 带越多，每条带分到的块越少 —— 这是冗余与容量的此消彼长关系，必须在界面上
/// 说清楚，否则用户调高冗余后莫名其妙「装不下」了。
int stripeCapacityBytes(int blocksWide, int blocksHigh, int stripes)
{
    const int rowsPerStripe = blocksHigh / stripes;
    const int blocksPerStripe = rowsPerStripe * blocksWide;
    // 每块 kBitsPerBlock 比特，换算成字节须**向上取整**。
    // 早期写成 `blocks * kBitsPerBlock / 8`（向下取整），在 kBitsPerBlock=1 时
    // 恒为 0，容量判断会全线失效 —— 而当时 kBitsPerBlock 是 8，所以没暴露。
    // 凡「比特数 → 字节数」的换算，一律要显式说明向上还是向下取整。
    return (blocksPerStripe * kBitsPerBlock + 7) / 8;
}

/// 逐块跑一遍「读块 → 正变换 → 回调 → 逆变换 → 写块」。
///
/// @param image 图像（写回）。
/// @param blockX 块列号。
/// @param blockY 块行号。
/// @param onBlock 在系数域上执行的操作，参数为该块的系数与下标。
template <typename BlockOp>
void processBlock(QImage &image, int blockX, int blockY, BlockOp onBlock)
{
    double luma[64] = {};
    double spectrum[64] = {};
    readLumaBlock(image, blockX, blockY, luma);
    forwardDct(luma, spectrum);
    onBlock(spectrum);
    inverseDct(spectrum, luma);
    writeLumaBlock(image, blockX, blockY, luma);
}

/// 报告进度的辅助函数。空回调时什么都不做。
void report(const ProgressCallback &progress, int percent)
{
    if (progress) {
        progress(percent);
    }
}

/// 让出当前线程，使后台执行期间 GUI 事件循环仍可运转。
///
/// 进度回调是在工作线程上被调用的；若 GUI 那边直接更新界面，必须有让出点，
/// 否则界面要等到整个运算结束才重绘，进度条等于没有。
void yieldIfNeeded(int blockIndex)
{
    if ((blockIndex & 0x3FF) == 0) {
        QThread::yieldCurrentThread();
    }
}

/// 读回一个块的**实际系数差值**（写入之后重新从图像计算，用于校验）。
///
/// 这是整个嵌入流程的**闭环环节**：系数域推完之后，逆变换会把误差扩散到 64 个
/// 像素，再经由「三通道平移 + 8 级灰阶量化」落回像素 —— 这条链路会吃掉一部分
/// 差值幅度。不做校验就会得到「嵌入报告成功、但提取读不出来」的结果
/// （见 docs/error_ledger.md 第 18 条）。
///
/// @return 写回后该块的实际差值；差值符号即读出的比特。
double measureBlockDifference(const QImage &image, int blockX, int blockY)
{
    double luma[64] = {};
    double spectrum[64] = {};
    readLumaBlock(image, blockX, blockY, luma);
    forwardDct(luma, spectrum);
    const auto [u, v] = kCoefficientPairs[0];
    return spectrum[v * kBlockSize + u] - spectrum[u * kBlockSize + v];
}

/// 在块 (blockX, blockY) 嵌入一个比特，并**回读校验裕度**；不足则回滚并加倍幅度重试。
///
/// 校验的是「写完之后重新读图像，系数差值是否仍达到 targetMargin」——
/// 注意**不是**「逆变换前的系数是否达到」：像素量化会削弱幅度，只看前者
/// 会得到一个「算得好好的」但存不下来的结果。
///
/// @param image 图像（原地修改，失败时已回滚）。
/// @param bit 要嵌入的比特。
/// @param strength 初始系数幅度。
/// @param targetMargin 期望的差值裕度。
/// @return 实际达到的裕度（供统计与诊断）。
double embedBlockWithMargin(QImage &image, int blockX, int blockY, bool bit, double strength,
                            double targetMargin)
{
    const int minX = blockX * kBlockSize;
    const int minY = blockY * kBlockSize;
    // 备份该块涉及的像素行，供回滚。块高最多 8 行，每行宽度有限，按行整备份。
    const int firstRow = std::max(0, minY);
    const int lastRow = std::min(image.height() - 1, minY + kBlockSize - 1);
    const int firstCol = std::max(0, minX);
    const int lastCol = std::min(image.width() - 1, minX + kBlockSize - 1);
    const int backupWidth = lastCol - firstCol + 1;
    std::vector<quint32> backup(static_cast<std::size_t>(lastRow - firstRow + 1)
                                * static_cast<std::size_t>(backupWidth));

    double attempt = strength;
    for (int round = 0; round < kEmbedVerifyMaxRounds; ++round) {
        // 备份 → 尝试写入
        for (int y = firstRow; y <= lastRow; ++y) {
            const uchar *scan = image.constScanLine(y);
            quint32 *row = &backup[static_cast<std::size_t>(y - firstRow) * backupWidth];
            for (int x = firstCol; x <= lastCol; ++x) {
                row[x - firstCol] =
                    qRgba(scan[x * 4 + 2], scan[x * 4 + 1], scan[x * 4], scan[x * 4 + 3]);
            }
        }

        processBlock(image, blockX, blockY,
                     [bit, attempt](double *spectrum) { modulatePair(spectrum, 0, bit, attempt); });

        // 回读校验
        const double actual = measureBlockDifference(image, blockX, blockY);
        const double achieved = bit ? actual : -actual;
        if (achieved >= targetMargin) {
            return achieved;
        }

        // 回滚
        for (int y = firstRow; y <= lastRow; ++y) {
            uchar *scan = image.scanLine(y);
            const quint32 *row = &backup[static_cast<std::size_t>(y - firstRow) * backupWidth];
            for (int x = firstCol; x <= lastCol; ++x) {
                const quint32 pixel = row[x - firstCol];
                scan[x * 4] = static_cast<uchar>(qBlue(pixel));
                scan[x * 4 + 1] = static_cast<uchar>(qGreen(pixel));
                scan[x * 4 + 2] = static_cast<uchar>(qRed(pixel));
                scan[x * 4 + 3] = static_cast<uchar>(qAlpha(pixel));
            }
        }
        attempt *= 2.0;
    }
    return -1.0;
}

} // namespace

int maxPixelDelta(const QImage &source, const QImage &embedded)
{
    if (source.isNull() || embedded.isNull() || source.size() != embedded.size()) {
        return 0;
    }
    int worst = 0;
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            const QRgb before = source.pixel(x, y);
            const QRgb after = embedded.pixel(x, y);
            worst = std::max(worst, std::abs(qRed(after) - qRed(before)));
            worst = std::max(worst, std::abs(qGreen(after) - qGreen(before)));
            worst = std::max(worst, std::abs(qBlue(after) - qBlue(before)));
        }
    }
    return worst;
}

QString validateCoefficientPairs()
{
    QSet<int> used;
    for (std::size_t i = 0; i < kCoefficientPairs.size(); ++i) {
        const auto [u, v] = kCoefficientPairs[i];
        if (u >= v) {
            return QStringLiteral("第 %1 对必须 u<v（否则与转置对撞），实际为 (%2,%3)")
                .arg(i)
                .arg(u)
                .arg(v);
        }
        if (u < 1 || v >= kBlockSize) {
            return QStringLiteral("第 %1 对 (%2,%3) 越界或触及直流行/列，须满足 1≤u<v≤7")
                .arg(i)
                .arg(u)
                .arg(v);
        }
        // (u,v) 用到的两个下标，与 (v,u) 完全相同 —— 故 u<v 是防撞的前提，
        // 这里再查一遍「不同对之间不重叠」，把约束落到可执行的形式上。
        const int indexA = v * kBlockSize + u;
        const int indexB = u * kBlockSize + v;
        if (used.contains(indexA) || used.contains(indexB)) {
            return QStringLiteral("第 %1 对 (%2,%3) 与更早的配对争用同一系数下标 %4/%5")
                .arg(i)
                .arg(u)
                .arg(v)
                .arg(indexA)
                .arg(indexB);
        }
        used.insert(indexA);
        used.insert(indexB);
    }
    return QString();
}

double dctRoundTripResidual()
{
    // 固定测试块：内容无规律才能覆盖到各个频率（含非对称的中间项），
    // 全零块会让任何归一化都「看起来正确」。
    double block[64] = {};
    quint32 state = 0x9E3779B9u;
    for (int i = 0; i < 64; ++i) {
        state = state * 1664525u + 1013904223u;
        block[i] = static_cast<double>((state >> 16) & 0xFFu);
    }

    double spectrum[64] = {};
    double restored[64] = {};
    forwardDct(block, spectrum);
    inverseDct(spectrum, restored);

    double worst = 0.0;
    for (int i = 0; i < 64; ++i) {
        worst = std::max(worst, std::abs(restored[i] - block[i]));
    }
    return worst;
}

int capacityBytes(const QSize &imageSize)
{
    if (imageSize.isEmpty()) {
        return 0;
    }
    const auto [blocksWide, blocksHigh] = blockGrid(imageSize);
    // 不做冗余时的理论上限：全部块都用来装载荷。
    return (blocksWide * blocksHigh * kBitsPerBlock + 7) / 8;
}

EmbedResult embedWatermark(const QImage &source, const QByteArray &payload, double strength,
                           int stripes, const ProgressCallback &progress)
{
    EmbedResult result;

    if (source.isNull()) {
        result.errorText = QStringLiteral("图像为空，无法嵌入水印");
        return result;
    }
    if (payload.isEmpty()) {
        result.errorText = QStringLiteral("水印内容为空，请先填写或自动生成");
        return result;
    }

    // 强度按像素域给定，这里换算成系数域幅度。
    const double strengthClamped = std::clamp(strength, kMinStrength, kMaxStrength);
    const double coefficientStrength = strengthClamped / kPixelToCoefficientRatio;
    const auto [blocksWide, blocksHigh] = blockGrid(source.size());

    if (blocksWide < 1 || blocksHigh < 1) {
        result.errorText = QStringLiteral("图像尺寸过小，放不下一块 8×8 像素");
        return result;
    }

    // 冗余条数：以调用方请求为准，但必须保证「每条带都装得下整份载荷」。
// 循环必须**从请求值往下走**，取最大的那个装得下的条数：1 条带容量最大、
// 永远装得下，若从 1 往上枚举就只会得到 1，冗余形同虚设。
    const int requested = std::clamp(stripes, 1, kMaxStripes);
    int usable = 1;
    for (int candidate = requested; candidate >= 1; --candidate) {
        if (stripeCapacityBytes(blocksWide, blocksHigh, candidate) >= payload.size()) {
            usable = candidate;
            break;
        }
    }
    // 连 1 条带都装不下才算真的失败（此时 usable 仍是 1，但容量不足）。
    if (stripeCapacityBytes(blocksWide, blocksHigh, 1) < payload.size()) {
        result.capacityBytes = capacityBytes(source.size());
        result.errorText = QStringLiteral("图像容量不足：当前最多可嵌入 %1 字节，水印为 %2 字节。"
                                          "请缩短水印内容或换一张更大的图。")
                               .arg(result.capacityBytes)
                               .arg(payload.size());
        return result;
    }
    const int stripesUsed = usable;

    // 逐比特展开载荷：高位在前，与写入顺序一致。
    std::vector<bool> bits;
    bits.reserve(static_cast<std::size_t>(payload.size()) * 8);
    for (char rawByte : payload) {
        const auto value = static_cast<unsigned char>(rawByte);
        for (int bitIndex = 7; bitIndex >= 0; --bitIndex) {
            bits.push_back(((value >> bitIndex) & 1u) != 0u);
        }
    }

    QImage target = source.convertToFormat(QImage::Format_RGB32);
    const int rowsPerStripe = blocksHigh / stripesUsed;
    // 实际要处理的块数：每条带写满载荷就停，不横扫整幅图。这样进度分母是
    // 「真正要干的活」而不是图的总块数，否则进度条会在开头冲到 90% 然后
    // 长时间不动 —— 那比没有进度条更让人以为卡死。
    const int blocksPerStripe = (static_cast<int>(bits.size()) + kBitsPerBlock - 1) / kBitsPerBlock;
    const int totalBlocksToProcess = blocksPerStripe * stripesUsed;
    int bitCursor = 0;
    int blockCount = 0;

    for (int stripe = 0; stripe < stripesUsed; ++stripe) {
        const int firstRow = stripe * rowsPerStripe;
        const int lastRow = (stripe == stripesUsed - 1) ? blocksHigh : (stripe + 1) * rowsPerStripe;

        for (int blockY = firstRow; blockY < lastRow; ++blockY) {
            for (int blockX = 0; blockX < blocksWide && bitCursor < static_cast<int>(bits.size());
                 ++blockX) {
                const bool bit = bits[static_cast<std::size_t>(bitCursor)];
                embedBlockWithMargin(target, blockX, blockY, bit, coefficientStrength,
                                     kEmbedTargetMargin);
                ++bitCursor;
                ++blockCount;
                // 每处理一块报一次进度（内部按百分比节流，见 report()）。
                report(progress, blockCount * 100 / totalBlocksToProcess);
                yieldIfNeeded(blockCount);
            }
        }

        // 每条带都从载荷开头重新写一遍，这就是冗余。
        bitCursor = 0;
    }

    report(progress, 100);
    result.ok = true;
    result.image = target;

    result.capacityBytes = capacityBytes(source.size());
    result.usedBytes = payload.size();
    result.blocks = blockCount;
    result.stripes = stripesUsed;
    return result;
}

/// 从已读出的比特流里解析「载荷总字节数」（头部 + 正文）。
///
/// 提取端必须知道**该读多少字节**才能停：嵌入端是「从载荷开头顺序写满即停」，
/// 图大于载荷时后面那些块根本没被写过，读它们等于在读原始图像的系数。所以这里
/// 从载荷头的长度字段（第 3–4 字节，大端）取真实长度，而不是把整条带读满。
///
/// 早期版本读满整条带再交给 parsePayload，于是图一大于载荷就长度对不上
/// （`raw.size() != 头 + 正文`）被 CRC 判死，症状是「嵌入永远成功、提取永远失败」。
///
/// @param bits 已读出的比特（至少 kPayloadHeaderBytes 个）。
/// @param capacityBytes 本条带的字节容量，作为长度上界（防止把噪声当成长度）。
/// @return 载荷总字节数；头部不合规时返回 0，调用方回退到「读满整条带」。
int expectedPayloadBytes(const std::vector<bool> &bits, int capacityBytes)
{
    // **单位：bits 是比特数，kPayloadHeaderBytes 是字节数**，故必须乘 8。
    // 少了这个 *8，本函数会在只读到 9 个比特时就被调用，此时长度字段
    // （第 3–4 字节）落在尚未读到的比特上，被下面的兜底填成 0 ->
    // bodyLength=0 -> 返回 0 -> 调用方回退到「读满整条带」，
    // 于是长度校验失败、报「未检测到水印」。
    if (bits.size() < static_cast<std::size_t>(kPayloadHeaderBytes) * 8) {
        return 0;
    }
    // 组装头部字节：大端、高位在前，与 buildPayload 的写法严格一致。
    int headerBytes[kPayloadHeaderBytes] = {};
    for (int index = 0; index < kPayloadHeaderBytes; ++index) {
        int value = 0;
        for (int offset = 0; offset < 8; ++offset) {
            const std::size_t bitIndex = static_cast<std::size_t>(index * 8 + offset);
            value = (value << 1) | (bitIndex < bits.size() && bits[bitIndex] ? 1 : 0);
        }
        headerBytes[index] = value;
    }
    // 魔数与版本先验一遍：不合规说明这根本不是我们的水印。
    if (headerBytes[0] != static_cast<quint8>(kPayloadMagicFirst)
        || headerBytes[1] != static_cast<quint8>(kPayloadMagicSecond)
        || headerBytes[2] != static_cast<quint8>(kPayloadVersion)) {
        return 0;
    }
    const int bodyLength = (headerBytes[3] << 8) | headerBytes[4];
    if (bodyLength <= 0 || bodyLength > kMaxPayloadBytes) {
        return 0;
    }
    const int total = kPayloadHeaderBytes + bodyLength;
    return total <= capacityBytes ? total : 0;
}

ExtractResult extractWatermark(const QImage &image, const ProgressCallback &progress)
{
    ExtractResult result;

    if (image.isNull()) {
        result.errorText = QStringLiteral("图像为空，无法提取水印");
        return result;
    }

    const auto [blocksWide, blocksHigh] = blockGrid(image.size());
    if (blocksWide < 1 || blocksHigh < 1) {
        result.errorText = QStringLiteral("图像尺寸过小，不含可用水印");
        return result;
    }

    const QImage source = image.convertToFormat(QImage::Format_RGB32);
    int attemptedStripes = 0;
    QByteArray bestPayload;

    // 盲检测：不知道嵌入时用了多少条带，就把 1..kMaxStripes 全试一遍。
    // 每条带读出的字节交给 parsePayload 严检（魔数 + 长度 + CRC），通过的才算数。
    for (int stripes = 1; stripes <= kMaxStripes; ++stripes) {
        const int rowsPerStripe = blocksHigh / stripes;
        if (rowsPerStripe < 1) {
            continue;
        }
        ++attemptedStripes;
        const int perStripeBytes = stripeCapacityBytes(blocksWide, blocksHigh, stripes);

        int recoveredHere = 0;
        for (int stripe = 0; stripe < stripes; ++stripe) {
            const int firstRow = stripe * rowsPerStripe;
            const int lastRow = (stripe == stripes - 1) ? blocksHigh : (stripe + 1) * rowsPerStripe;

            std::vector<bool> bits;
            // 只读到「整条带装满」为止—— 但**不能**假设载荷正好占满整条带：
            // 嵌入端是「从载荷开头顺序写满即停」，图大一点就会只写前面几块，
            // 后面全是未写过的原始系数。于是这里必须**渐进读取**：先读够一个头，
            // 从长度字段得知真实字节数，再补读到那么多为止。
            //
            // 早期版本把整条带全部读出再交给 parsePayload，于是图一大于载荷就
            // 长度对不上（raw.size() != 头 + 正文）被 CRC 判死，症状是
            // 「嵌入永远成功、提取永远失败」—— 见 docs/error_ledger.md 第 13 条。
            bits.reserve(static_cast<std::size_t>(perStripeBytes) * 8);
            int blockCount = 0;
            int expectedBytes = 0; ///< 由载荷头的长度字段决定；0 =尚未读到合规的头
            for (int blockY = firstRow; blockY < lastRow; ++blockY) {
                for (int blockX = 0; blockX < blocksWide; ++blockX) {
                    double luma[64] = {};
                    double spectrum[64] = {};
                    readLumaBlock(source, blockX, blockY, luma);
                    forwardDct(luma, spectrum);
                    for (int pairIndex = 0; pairIndex < kBitsPerBlock; ++pairIndex) {
                        bits.push_back(demodulatePair(spectrum, pairIndex));
                    }
                    ++blockCount;
                    yieldIfNeeded(blockCount);

                    // 只在 stripes == 1 时记录：嵌入端在本图上用的是 1 条带
                    // （stripeCapacityBytes 判定），而提取端会遍历 1..kMaxStripes
                    // 六种布局，记录最后一个（stripes=6）的读数等于在观察
                    // 「另一种布局下的首块」—— 这正是之前那个假矛盾的来源。
                    if (stripes == 1 && blockCount == 1 && blockX == 0) {
                    }
                    // 已经读到**完整的头部**（9 字节 = 72 比特，注意单位）才能确定载荷长度。
                    // 早前这里误写成 bits.size() >= kPayloadHeaderBytes（拿比特数
                    // 与字节数比），导致只读到 9 比特就去解析长度字段。
                    if (expectedBytes == 0
                        && bits.size() >= static_cast<std::size_t>(kPayloadHeaderBytes) * 8) {
                        expectedBytes = expectedPayloadBytes(bits, perStripeBytes);
                    }
                    // 读够了就停，不必横扫整条带（后面都是未写过的原始系数）。
                    if (expectedBytes > 0
                        && bits.size() >= static_cast<std::size_t>(expectedBytes) * 8) {
                        break;
                    }
                }
                if (expectedBytes > 0
                    && bits.size() >= static_cast<std::size_t>(expectedBytes) * 8) {
                    break;
                }
            }

            // 按载荷头的长度字段决定读多少字节；头部本身就不可信（魔数/版本
            // 不对）时回退到「读满整条带」，交给 parsePayload 判死。
            const int bytesToRead = (expectedBytes > 0) ? expectedBytes : perStripeBytes;

            // 比特流打包回字节：大端、高位在前，与写入顺序严格对应。
            QByteArray raw;
            raw.reserve(bytesToRead);
            for (int index = 0; index < bytesToRead; ++index) {
                int value = 0;
                for (int offset = 0; offset < 8; ++offset) {
                    const std::size_t bitIndex = static_cast<std::size_t>(index * 8 + offset);
                    if (bitIndex >= bits.size()) {
                        break;
                    }
                    value = (value << 1) | (bits[bitIndex] ? 1 : 0);
                }
                raw.append(static_cast<char>(static_cast<unsigned char>(value)));
            }
            // 只记录 stripes == 1（嵌入端实际使用的布局）。若每次尝试都覆盖，
            // 最终留下的是最后一次（stripes=6）的读数 —— 那是另一种块带边界，
            // 观察它等于在看一个不存在的对象（这已误导排查多轮）。
            if (stripes == 1 && raw.size() >= 3) {
            }

            QByteArray payload;
            QString error;
            if (parsePayload(raw, &payload, &error) && !payload.isEmpty()) {
                ++recoveredHere;
                if (bestPayload.isEmpty()) {
                    bestPayload = payload;
                } else if (payload != bestPayload) {
                    // 两条带解出**不同**内容：说明有多份水印（用户嵌过不止一次）。
                    // 保留先解出的那条，但结果仍然通过 CRC，可信。
                    continue;
                }
            }
        }

        report(progress, stripes * 100 / kMaxStripes);
        if (recoveredHere > 0) {
            result.ok = true;
            result.payload = bestPayload;
            result.recoveredStripes = recoveredHere;
            result.totalStripes = attemptedStripes;
            return result;
        }
    }

    result.totalStripes = attemptedStripes;
    result.errorText =
        QStringLiteral("未检测到有效水印。图片可能被重新压缩或裁剪过（这两者都会破坏水印），"
                       "或者它本来就没有嵌入过水印。");
    return result;
}

} // namespace imgwatermark
