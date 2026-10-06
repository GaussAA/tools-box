#include "core/EngineInstall.h"

#include <QFile>

namespace videodl {

namespace {

/// 两种内核共用一套处置流程，只有「怎么算合格」不同。
EngineInstallResult install(const QString &partPath, const QString &targetPath,
                            EngineFileProblem (*check)(qint64, const QByteArray &))
{
    EngineInstallResult result;

    // 读文件头与大小；读不到（文件没了、磁盘满）时 head 为空，校验自然判不过。
    QFile partFile(partPath);
    QByteArray head;
    if (partFile.open(QIODevice::ReadOnly)) {
        head = partFile.read(4);
        result.size = partFile.size();
        // 必须先关掉：**Windows 上打开着的文件既不能改名也不能删除** ——
        // 关着的话下面的 QFile::remove / rename 会一律失败，表现为「好文件也存不下、
        // 坏文件也删不掉」。这个坑是 tst_engineinstall 首次运行就抓出来的。
        partFile.close();
    }

    result.problem = check(result.size, head);
    if (result.problem != EngineFileProblem::None) {
        // 坏文件一律删掉：留着就等于给下一次启动埋一颗雷（见头文件注释）。
        QFile::remove(partPath);
        result.status = EngineInstallStatus::RejectedBadFile;
        return result;
    }

    // 就位：先删旧的目标（内核更新是覆盖式），再把半截改名过来。
    QFile::remove(targetPath);
    if (!QFile::rename(partPath, targetPath)) {
        QFile::remove(partPath);
        result.status = EngineInstallStatus::RenameFailed;
        return result;
    }

    result.status = EngineInstallStatus::Installed;
    return result;
}

} // namespace

EngineInstallResult installYtDlp(const QString &partPath, const QString &targetPath)
{
    return install(partPath, targetPath, &checkYtDlpBinary);
}

EngineInstallResult installFfmpegZip(const QString &partPath, const QString &targetPath)
{
    return install(partPath, targetPath, &checkFfmpegZip);
}

} // namespace videodl
