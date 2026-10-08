#include "WatermarkText.h"

#include "core/Stego.h"

#include "core/StegoPayload.h"

#include <QBuffer>
#include <QImage>
#include <QTest>

// 图片数字水印插件的纯逻辑（plugins/imgwatermark/core）用例。
//
// 测三件事，从便宜到贵：
//   1. 载荷层的四道闸（魔数/版本/长度/CRC）各自能否拦住坏数据；
//   2. DCT 正逆变换是否自洽（往返回到原值）—— 这是整个算法的地基；
//   3. 端到端往返：嵌入 → 提取，能否一字不差取回原文。
//
// 第3 条是真正的价值所在：中间任何一环写错（比如比特顺序反了、系数配对表在
// 嵌入与提取两侧不一致），都会让它失败；而这类错误用肉眼是查不出来的。
class TestStego : public QObject
{
    Q_OBJECT

private slots:
    void crc32MatchesKnownVector();
    void payloadRoundTrips();
    void payloadRejectsEmptyBody();
    void payloadRejectsBadMagic();
    void payloadRejectsWrongVersion();
    void payloadRejectsTruncatedInput();
    void payloadRejectsCorruptedBody();
    void capacityGrowsWithImageSize();
    void capacityIsZeroForNullSize();
    void autoWatermarkTextCarriesTraceableFields();
    void outputFormatsCoverLosslessAndLossy();
    void coefficientPairsAreDisjoint();
    void embedStaysInvisible();
    void dctRoundTripsExactly();
    void embedRoundTripsInMemory();
    void embedRoundTripsThroughPng();
    void embedSurvivesJpegCompression();
    void embedReportsEmptyPayload();
    void embedRejectsNullImage();
    void embedReportsInsufficientCapacity();
    void extractFailsOnPlainImage();
    void extractRecoversAfterStripDamage();
    void embedFallsBackWhenStripesDoNotFit();
    void embedDoesNotModifySource();
    void payloadLimitIsRespected();
};

namespace {

/// 造一张有内容（不是纯色）的测试图。
///
/// 纯色图不行：整幅只有直流分量，8×8 块里几乎没有中频系数可调，嵌进去也读不
/// 出来。用确定性伪随机噪点，保证每次跑的图完全一致（可复现）。
QImage makeTestImage(int width, int height)
{
    QImage image(width, height, QImage::Format_RGB32);
    quint32 state = 0x12345678u;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            state = state * 1664525u + 1013904223u;
            const int base = static_cast<int>((state >> 16) & 0xFFu);
            image.setPixel(x, y, qRgba(base, (base * 3) % 256, (base * 7) % 256, 255));
        }
    }
    return image;
}

/// 绕一圈存成 PNG 再读回来，模拟真实的「存盘 → 重新打开」链路。
///
/// 水印是嵌在像素里的，所以往返是**必需**的一步：真实使用中用户一定先存盘、
/// 之后才拿文件去提取。不走这一步的测试会漏掉「存盘过程本身弄坏水印」这类缺陷。
QImage roundTrip(const QImage &image, const char *format, int quality = -1)
{
    QByteArray bytes;
    QBuffer out(&bytes);
    if (!out.open(QIODevice::WriteOnly)) {
        return QImage();
    }
    const bool saved = quality < 0 ? image.save(&out, format) : image.save(&out, format, quality);
    out.close();
    if (!saved) {
        return QImage();
    }

    QBuffer in(&bytes);
    if (!in.open(QIODevice::ReadOnly)) {
        return QImage();
    }
    QImage restored;
    restored.load(&in, format);
    return restored;
}

} // namespace

void TestStego::crc32MatchesKnownVector()
{
    // CRC32 的标准检验向量：对ASCII "123456789" 应得 0xCBF43926。
    // 钉住这个值是为了防止「多项式或初值写错」——那种错误同样能自洽往返，
    // 只在跨工具对比时才暴露，而那时已经很难定位。
    QCOMPARE(imgwatermark::crc32(QByteArrayLiteral("123456789")), 0xCBF43926u);
    // 空串也有确定值（0），且与「非 0 初值」无关的部分要自洽。
    QCOMPARE(imgwatermark::crc32(QByteArray()), 0x00000000u);
}

void TestStego::payloadRoundTrips()
{
    const QByteArray body = QByteArrayLiteral("ToolBox \xE5\x9C\xA8\xE5\x90\xA7");
    const QByteArray payload = imgwatermark::buildPayload(body);

    QVERIFY(!payload.isEmpty());
    QCOMPARE(payload.size(), imgwatermark::kPayloadHeaderBytes + body.size());
    QCOMPARE(payload.at(0), imgwatermark::kPayloadMagicFirst);
    QCOMPARE(payload.at(1), imgwatermark::kPayloadMagicSecond);

    QByteArray decoded;
    QString error;
    QVERIFY2(imgwatermark::parsePayload(payload, &decoded, &error),
             qPrintable(QStringLiteral("解析失败：%1").arg(error)));
    QCOMPARE(decoded, body);
}

void TestStego::payloadRejectsEmptyBody()
{
    // 空正文没有意义（嵌入进去也读不出东西），必须当成失败而不是「合法空载荷」。
    QVERIFY(imgwatermark::buildPayload(QByteArray()).isEmpty());
}

void TestStego::payloadRejectsBadMagic()
{
    QByteArray payload = imgwatermark::buildPayload(QByteArrayLiteral("hello"));
    payload[0] = 'X';

    QByteArray decoded;
    QString error;
    QVERIFY(!imgwatermark::parsePayload(payload, &decoded, &error));
    QVERIFY(!error.isEmpty());
}

void TestStego::payloadRejectsWrongVersion()
{
    QByteArray payload = imgwatermark::buildPayload(QByteArrayLiteral("hello"));
    payload[2] = static_cast<char>(imgwatermark::kPayloadVersion + 1);

    QByteArray decoded;
    QString error;
    QVERIFY(!imgwatermark::parsePayload(payload, &decoded, &error));
    QVERIFY(error.contains(QStringLiteral("版本")));
}

void TestStego::payloadRejectsTruncatedInput()
{
    const QByteArray payload = imgwatermark::buildPayload(QByteArrayLiteral("hello world"));

    QByteArray decoded;
    QString error;
    // 比头还短：连头都没读全。
    QVERIFY(!imgwatermark::parsePayload(payload.left(4), &decoded, &error));
    // 头完整但正文被截断：长度字段还在，正文少了一截，必须拦住。
    QVERIFY(!imgwatermark::parsePayload(payload.left(payload.size() - 3), &decoded, &error));
}

void TestStego::payloadRejectsCorruptedBody()
{
    QByteArray payload = imgwatermark::buildPayload(QByteArrayLiteral("copyright 2026"));
    payload[payload.size() - 1] = 'X'; // 改动正文里的一个字节

    QByteArray decoded;
    QString error;
    // CRC32 就是为这一刻存在的：内容变了但长度没变，只有校验能发现。
    QVERIFY(!imgwatermark::parsePayload(payload, &decoded, &error));
    QVERIFY(error.contains(QStringLiteral("校验")));
}

void TestStego::capacityGrowsWithImageSize()
{
    // 容量与块数成正比，即与像素数成正比。每块 1 比特，容量单位是块。
    const int small = imgwatermark::capacityBytes(QSize(64, 64));
    const int large = imgwatermark::capacityBytes(QSize(256, 256));
    QVERIFY(small > 0);
    QVERIFY(large > small);
    QCOMPARE(large, small * 16); // 边长 4 倍 → 面积 16 倍
}

void TestStego::capacityIsZeroForNullSize()
{
    QCOMPARE(imgwatermark::capacityBytes(QSize(0, 0)), 0);
}

void TestStego::autoWatermarkTextCarriesTraceableFields()
{
    // 自动水印是「懒得打字」时的默认内容，也是本工具唯一的溯源信息生成处。
    // 四要素（文件名 / 尺寸 / 体积 / 时间）缺任何一项，日后就认不出原图 ——
    // 只有文件名时同名文件会混淆，只有时间时同批导出无法区分。这条断言把它们钉住。
    const QDateTime when(QDate(2026, 10, 7), QTime(12, 34, 56));
    const QString text =
        autoWatermarkText(QStringLiteral("holiday-photo"), QSize(4032, 3024), 3145728, when);

    QVERIFY(text.contains(QStringLiteral("holiday-photo")));
    QVERIFY(text.contains(QStringLiteral("4032")));
    QVERIFY(text.contains(QStringLiteral("3024")));
    QVERIFY(text.contains(QStringLiteral("3145728")));
    QVERIFY(text.contains(QStringLiteral("2026-10-07")));

    // 尺寸未知时省略该项，但其余三项仍须齐全 —— 不能因为一个字段缺失就整体退化。
    const QString noSize = autoWatermarkText(QStringLiteral("x"), QSize(), 100, when);
    QVERIFY(noSize.contains(QStringLiteral("x")));
    QVERIFY(noSize.contains(QStringLiteral("100")));
    QVERIFY(noSize.contains(QStringLiteral("2026-10-07")));
}

void TestStego::outputFormatsCoverLosslessAndLossy()
{
    // 界面据 supportsQuality() 决定质量滑块是否可用；据 suffix() 决定落盘扩展名。
    // 两者任一为空都会让「另存为」静默出错，所以这里钉住表的完整性。
    const QVector<OutputFormat> &formats = outputFormats();
    QVERIFY(!formats.isEmpty());

    QStringList suffixes;
    bool hasLossy = false;
    bool hasLossless = false;
    for (const OutputFormat &format : formats) {
        QVERIFY(!format.label().isEmpty());
        QVERIFY(!format.suffix().isEmpty());
        suffixes << format.suffix();
        hasLossy = hasLossy || format.supportsQuality();
        hasLossless = hasLossless || !format.supportsQuality();
    }
    QVERIFY(hasLossy);
    QVERIFY(hasLossless);
    // 扩展名不应重复，否则下拉框会出现两个同名项而用户无从分辨。
    QCOMPARE(suffixes.size(), QSet<QString>(suffixes.begin(), suffixes.end()).size());
}

void TestStego::coefficientPairsAreDisjoint()
{
    // 配对表混进 (u,v) 与 (v,u) 时，两者落到相同的两个下标却被分配给两个不同的
    // 比特 —— 嵌入与提取自相矛盾，症状是「每 8 比特固定错一位 + CRC 失败」。
    // 这条断言把那张表的可断言性质守住，避免同类错误再发生。
    const QString problem = imgwatermark::validateCoefficientPairs();
    QVERIFY2(problem.isEmpty(), qPrintable(problem));
}

void TestStego::embedStaysInvisible()
{
    // 「肉眼不可见」是本工具的立身之本，却是**定性**需求 —— 一旦强度定义在
    // 系数域而用户按像素理解，代码照样能跑、提取照样能过，只是图上出现了
    // 肉眼可见的块状纹理（见 docs/error_ledger.md 第 10 条）。把这条需求落成
    // 一个数值上限，它才成为可回归的门禁。
    //
    // 上限 48（255 量纲的 19%）是「肉眼不刻意对比即看不出」的经验边界。
    // 取「每块一对」后实测默认强度下约33 级，故留出余量；上限一旦被突破，
    // 说明系数调制或写入路径又回到了「块内多对叠加」的老问题上。
    const QImage source = makeTestImage(256, 256);
    const QByteArray payload = imgwatermark::buildPayload(QByteArrayLiteral("invisible"));

    const imgwatermark::EmbedResult embedded =
        imgwatermark::embedWatermark(source, payload, imgwatermark::kDefaultStrength, 1);
    QVERIFY2(embedded.ok, qPrintable(embedded.errorText));

    const int delta = imgwatermark::maxPixelDelta(source, embedded.image);
    QVERIFY2(delta <= 48,
             qPrintable(QStringLiteral("像素改动过大(%1)，水印将肉眼可见").arg(delta)));
    qInfo() << "max pixel delta at default strength:" << delta;

    // 最强档位同样要守住不可见 —— 否则滑块拉到头就会毁掉图片。
    const imgwatermark::EmbedResult strongest =
        imgwatermark::embedWatermark(source, payload, imgwatermark::kMaxStrength, 1);
    QVERIFY(strongest.ok);
    const int strongestDelta = imgwatermark::maxPixelDelta(source, strongest.image);
    QVERIFY2(
        strongestDelta <= 64,
        qPrintable(QStringLiteral("最大强度下像素改动过大(%1)，已肉眼可见").arg(strongestDelta)));
}

void TestStego::dctRoundTripsExactly()
{
    // 正逆必须互逆。这条断言守着整个算法的地基：一旦归一化写错（例如逆变换
    // 直接复用正变换），残差会从 1e-9 量级跳到几十，症状表现为
    // 「嵌入成功但提不出水印」——极难定位，所以在此就地钉住。
    const double residual = imgwatermark::dctRoundTripResidual();
    QVERIFY2(residual < 1e-9,
             qPrintable(QStringLiteral("正逆变换往返残差过大：%1").arg(residual, 0, 'e', 3)));
}

void TestStego::embedRoundTripsInMemory()
{
    // 不经存盘直接提取。这一条把「算法本身」与「存盘环节」分开：
    // 若它在内存里就失败，问题在嵌入/提取；若它过而下面那条 PNG 往返失败，
    // 问题才在存盘或颜色格式上。没有这条对照，两种故障会混成一个症状。
    const QImage source = makeTestImage(256, 256);
    const QByteArray body = QByteArrayLiteral("in-memory");
    const QByteArray payload = imgwatermark::buildPayload(body);

    const imgwatermark::EmbedResult embedded =
        imgwatermark::embedWatermark(source, payload, imgwatermark::kDefaultStrength, 1);
    QVERIFY2(embedded.ok, qPrintable(embedded.errorText));

    const imgwatermark::ExtractResult extracted = imgwatermark::extractWatermark(embedded.image);
    QVERIFY2(extracted.ok, qPrintable(extracted.errorText));
    QCOMPARE(extracted.payload, body);
}

void TestStego::embedRoundTripsThroughPng()
{
    // 端到端的核心用例：PNG 无损，嵌入 → 存盘 → 读回 → 提取，应当一字不差取回。
    const QImage source = makeTestImage(256, 256);
    const QByteArray body = QByteArrayLiteral("\xE5\x8C\x97\xE4\xBA\xAC Tech 2026");
    const QByteArray payload = imgwatermark::buildPayload(body);

    const imgwatermark::EmbedResult embedded =
        imgwatermark::embedWatermark(source, payload, imgwatermark::kDefaultStrength, 1);
    QVERIFY2(embedded.ok, qPrintable(embedded.errorText));
    QVERIFY(!embedded.image.isNull());
    QCOMPARE(embedded.usedBytes, payload.size());

    const QImage restored = roundTrip(embedded.image, "PNG");
    QVERIFY(!restored.isNull());
    const imgwatermark::ExtractResult extracted = imgwatermark::extractWatermark(restored);
    QVERIFY2(extracted.ok, qPrintable(extracted.errorText));
    QCOMPARE(extracted.payload, body);
}

void TestStego::embedSurvivesJpegCompression()
{
    // JPEG 是有损的，水印强度足够时应当仍能取回 —— 这正是选 DCT 而非 LSB 的
    // 理由（LSB 在 JPEG 下存活率约等于 0）。
    const QImage source = makeTestImage(256, 256);
    const QByteArray body = QByteArrayLiteral("dct-watermark");
    const QByteArray payload = imgwatermark::buildPayload(body);

    const imgwatermark::EmbedResult embedded =
        imgwatermark::embedWatermark(source, payload, imgwatermark::kDefaultStrength, 1);
    QVERIFY2(embedded.ok, qPrintable(embedded.errorText));

    const QImage compressed = roundTrip(embedded.image, "JPEG", 92);
    QVERIFY(!compressed.isNull());
    const imgwatermark::ExtractResult extracted = imgwatermark::extractWatermark(compressed);
    QVERIFY2(extracted.ok, qPrintable(extracted.errorText));
    QCOMPARE(extracted.payload, body);
}

void TestStego::embedReportsEmptyPayload()
{
    const imgwatermark::EmbedResult result =
        imgwatermark::embedWatermark(makeTestImage(64, 64), QByteArray(), 16.0, 1);

    QVERIFY(!result.ok);
    QVERIFY(!result.errorText.isEmpty());
    QVERIFY(result.image.isNull());
}

void TestStego::embedRejectsNullImage()
{
    const imgwatermark::EmbedResult result =
        imgwatermark::embedWatermark(QImage(), imgwatermark::buildPayload(QByteArrayLiteral("x")),
                                     imgwatermark::kMinStrength, 1);

    QVERIFY(!result.ok);
    QVERIFY(!result.errorText.isEmpty());
}

void TestStego::embedReportsInsufficientCapacity()
{
    // 一张 16×16 的图只有 2×2 = 4 块 = 4 字节容量，装不下一个正常的载荷头+正文。
    // 这类失败必须**明确报出来**，而不是截断写入后假装成功。
    const QImage tiny = makeTestImage(16, 16);
    const QByteArray payload =
        imgwatermark::buildPayload(QByteArrayLiteral("this is far too long for a 16x16 image"));

    QVERIFY(imgwatermark::capacityBytes(tiny.size()) < payload.size());
    const imgwatermark::EmbedResult result =
        imgwatermark::embedWatermark(tiny, payload, imgwatermark::kMinStrength, 1);
    QVERIFY(!result.ok);
    QVERIFY(result.errorText.contains(QStringLiteral("容量")));
}

void TestStego::extractFailsOnPlainImage()
{
    // 没嵌过水印的图绝不能被「解出」一个假水印 —— 那会让用户误判图片来源。
    // 随机噪点图的比特流恰好凑出合法魔数的概率极低，这里断言的是这条防线存在。
    const imgwatermark::ExtractResult result =
        imgwatermark::extractWatermark(makeTestImage(256, 256));

    QVERIFY(!result.ok);
    QVERIFY(result.payload.isEmpty());
    QVERIFY(!result.errorText.isEmpty());
}

void TestStego::extractRecoversAfterStripDamage()
{
    // 冗余的意义：把嵌入后的图上半部分整片涂掉，下半部分（另一条带）仍应可解。
    //
    // 用 512×512：它有 64×64 = 4096 块，分成 4 带后每带仍有 1024 块 = 1024 字节
    // 容量，足够放载荷。早先用 256×256（每带仅 256 字节）时，4 带装不下而
    // core 正确地回退到了 1 带 —— 那不是缺陷，是「冗余与容量此消彼长」的
    // 正常体现，测试应当选一张真能容下 4 带的图，而不是去迁就实现的错误。
    const QImage source = makeTestImage(512, 512);
    const QByteArray body = QByteArrayLiteral("redundant-strip");
    const QByteArray payload = imgwatermark::buildPayload(body);

    const imgwatermark::EmbedResult embedded =
        imgwatermark::embedWatermark(source, payload, imgwatermark::kDefaultStrength, 4);
    QVERIFY2(embedded.ok, qPrintable(embedded.errorText));
    QCOMPARE(embedded.stripes, 4);

    QImage damaged = embedded.image;
    // 覆盖顶部 45%，模拟裁剪/涂抹。
    for (int y = 0; y < damaged.height() * 45 / 100; ++y) {
        for (int x = 0; x < damaged.width(); ++x) {
            damaged.setPixel(x, y, qRgb(255, 255, 255));
        }
    }

    const imgwatermark::ExtractResult extracted = imgwatermark::extractWatermark(damaged);
    QVERIFY2(extracted.ok, qPrintable(extracted.errorText));
    QCOMPARE(extracted.payload, body);
}

void TestStego::embedFallsBackWhenStripesDoNotFit()
{
    // 冗余与容量此消彼长：小图分 4 带后每带容不下载荷时，core 应当**回退到
    // 更少的条数**而不是报错，更不是截断写入后假装成功。这是有意的设计契约，
    // 固化成用例，免得日后有人把它当缺陷「修」掉。
    //每块 1 比特，故容量单位是「块」：128×128 = 16×16 = 256 块。
    //分 4 带后每带仅 64 块 = 8 字节，装不下「头+正文」，必然回退。
    const QImage small = makeTestImage(128, 128);
    const QByteArray payload = imgwatermark::buildPayload(QByteArrayLiteral("fallback"));

    const imgwatermark::EmbedResult embedded =
        imgwatermark::embedWatermark(small, payload, imgwatermark::kDefaultStrength, 4);
    QVERIFY2(embedded.ok, qPrintable(embedded.errorText));
    QVERIFY(embedded.stripes < 4);
    QVERIFY(embedded.stripes >= 1);

    // 回退出来的结果必须仍然可解 —— 否则「回退」只是掩盖了截断。
    const imgwatermark::ExtractResult extracted = imgwatermark::extractWatermark(embedded.image);
    QVERIFY2(extracted.ok, qPrintable(extracted.errorText));
    QCOMPARE(extracted.payload, payload.mid(imgwatermark::kPayloadHeaderBytes));
}

void TestStego::embedDoesNotModifySource()
{
    // 接口契约：embedWatermark 不动传入的原图（调用方往往还要拿原图另作他用）。
    const QImage source = makeTestImage(256, 256);
    const QImage before = source;

    const QByteArray payload = imgwatermark::buildPayload(QByteArrayLiteral("immutable"));
    const imgwatermark::EmbedResult embedded =
        imgwatermark::embedWatermark(source, payload, imgwatermark::kDefaultStrength, 1);
    QVERIFY(embedded.ok);

    QCOMPARE(source.size(), before.size());
    QVERIFY(source == before);
}

void TestStego::payloadLimitIsRespected()
{
    // 长度字段只有 2 字节，超过上限的正文必须被 buildPayload 拒绝，
    // 否则会静默截断，嵌入的水印与用户输入不一致 —— 这类「悄悄少了一截」
    // 最难被发现。
    const QByteArray tooLarge(imgwatermark::kMaxPayloadBytes + 1, 'x');
    QVERIFY(imgwatermark::buildPayload(tooLarge).isEmpty());

    const QByteArray atLimit(imgwatermark::kMaxPayloadBytes, 'x');
    QVERIFY(!imgwatermark::buildPayload(atLimit).isEmpty());
}

QTEST_APPLESS_MAIN(TestStego)

#include "tst_stego.moc"
