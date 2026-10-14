#include "RawProvider.h"

#include <QStringList>

// 已知相机 RAW 扩展名(大小写不敏感);非穷举,仅覆盖主流
static const QStringList kRawExtensions = {
    "dng", "cr2", "cr3", "nef", "nrw", "arw", "sr2", "srw",
    "rw2", "raf", "orf", "pef", "dcr", "kdc", "mrw", "3fr",
    "iiq", "x3f", "rwl", "r3d", "crw", "erf",
};

bool RawDecoder::isRawFile(const QString &path)
{
    const int dot = path.lastIndexOf('.');
    if (dot < 0)
        return false;
    const QString ext = path.mid(dot + 1).toLower();
    return kRawExtensions.contains(ext);
}
