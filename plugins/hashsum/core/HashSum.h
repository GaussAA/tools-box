#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// 哈希与校验和插件里「摘要计算」的那部分纯逻辑。
//
// 判定标准见 docs/architecture.md §3：凡是能脱离 QWidget 表达的逻辑都放进
// core/、编成只链接 Qt6::Core 的静态库 —— 误用界面类会直接编译失败。
//
// 本文件不得 include 任何 QtWidgets 头文件。

namespace hashsum {

/// 支持的算法，顺序即界面上的排列顺序。
enum class Algorithm {
    Md5,
    Sha1,
    Sha256,
    Sha512,
};

/// 算法的专有名词（MD5 / SHA-1 …）—— 不翻译，也不是 tr() 的对象。
QString algorithmName(Algorithm algo);

/// 全部算法，按上面的顺序。界面用它生成勾选框，避免两处各列一份。
QList<Algorithm> allAlgorithms();

/// 对一段字节算摘要，返回**小写**十六进制。
QString hashBytes(const QByteArray &data, Algorithm algo);

/// 算文件摘要时的失败原因。
enum class FileError {
    None,       ///< 成功
    NotAFile,   ///< 路径不存在，或不是普通文件
    CannotOpen, ///< 打不开（权限等）
    TooLarge,   ///< 超过调用方给的上限
};

/// 文件摘要的结果。ok 为假时看 error，由 View 翻成界面文案。
struct HashResult
{
    bool ok = false;
    QString text;
    FileError error = FileError::None;
};

/// 分块读文件算摘要。
///
/// 分块而不是 readAll()：一次性把整份文件读进内存，遇到几百 MB 的文件就会
/// 先撑爆内存再算。maxBytes 为 0 表示不限制（调用方自己决定要不要限）。
HashResult hashFile(const QString &path, Algorithm algo, qint64 maxBytes = 0);

} // namespace hashsum
