#include "clean_text.h"

#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace
{

// ── Stop words ──

static const QSet<QString> &chineseStopWords()
{
    static const QSet<QString> w = {
        "一个", "可以", "没有", "如果",   "因为", "所以", "但是", "而且",
        "然后", "应该", "需要", "已经",   "正在", "还是", "只是", "可是",
        "不过", "什么", "怎么", "为什么", "如何", "这个", "那个", "这些",
        "那些", "这里", "那里", "他们",   "它们", "她们", "自己", "知道",
        "成为", "就是", "不是", "都是",   "也是", "还有", "在于", "对于",
        "关于", "之后", "之前", "同时",   "其中", "通过", "虽然", "当时",
        "并且", "以及", "或者", "包括",   "基础", "之间", "作为", "可能",
        "进行", "提供", "支持", "使用",   "由于", "这样", "那样", "方面",
        "部分", "功能", "表示", "进入",   "选择", "删除", "添加", "新建",
        "打开", "关闭", "保存", "取消",   "确定", "确认", "上传", "下载",
        "设置", "更多", "全部", "其他",   "编辑", "查看", "帮助", "返回",
        "完成", "提交", "重置", "搜索",   "筛选", "排序", "应用", "默认",
        "当前", "项目", "文件", "目录",   "路径", "名称", "类型", "大小",
        "修改", "创建", "访问", "属性",   "信息", "状态", "错误", "警告",
        "成功", "失败", "等待", "加载",   "处理", "输出", "输入", "读取",
        "写入", "复制", "粘贴", "剪切",   "撤销", "重做", "替换", "查找",
        "转到", "行号", "列号", "位置",   "消息", "内容", "参数", "版本",
        "记住", "重试", "忽略", "中止",   "刷新", "退出", "选项", "首选项",
    };
    return w;
}

static const QSet<QString> &englishStopWords()
{
    static const QSet<QString> w = {
        "the",   "and",    "for",   "are",   "but",    "not",     "you",
        "all",   "can",    "has",   "was",   "were",   "had",     "did",
        "will",  "may",    "its",   "that",  "this",   "with",    "from",
        "have",  "been",   "more",  "some",  "they",   "them",    "their",
        "what",  "when",   "where", "which", "who",    "how",     "than",
        "into",  "should", "could", "about", "also",   "just",    "over",
        "such",  "each",   "only",  "other", "than",   "then",    "very",
        "well",  "here",   "there", "these", "those",  "does",    "must",
        "shall", "said",   "now",   "still", "even",   "much",    "too",
        "way",   "make",   "made",  "like",  "long",   "take",    "used",
        "back",  "many",   "same",  "first", "last",   "next",    "new",
        "old",   "good",   "high",  "low",   "big",    "small",   "own",
        "see",   "set",    "put",   "get",   "let",    "part",    "end",
        "while", "since",  "until", "after", "before", "between", "under",
        "above", "down",   "off",   "out",   "top",    "bottom",  "line",
        "file",  "run",    "use",   "try",   "one",    "two",     "also",
        "per",   "say",    "got",
    };
    return w;
}

// ── Rare word-start patterns ──

static const QSet<QString> &rareStartBigrams()
{
    static const QSet<QString> s = {
        "rr", "nd", "kl", "nk", "nt", "ns", "nc", "nv", "nm", "nl", "nr", "rb",
        "rc", "rd", "rf", "rg", "rj", "rk", "rm", "rp", "rs", "rt", "rv", "rw",
        "rz", "lb", "lc", "ld", "lf", "lg", "lk", "lm", "ln", "lp", "lq", "lr",
        "ls", "lt", "lv", "lw", "mb", "mc", "mf", "mg", "mj", "mk", "ml", "mp",
        "mq", "mr", "ms", "mt", "mv", "mw", "nb", "nf", "nh", "nj", "np", "nq",
        "nr", "nv", "nw", "nx", "nz", "pb", "pd", "pf", "pj", "pk", "pn", "pp",
        "pq", "pt", "pw", "px", "sb", "sd", "sf", "sg", "sj", "ss", "sv", "sw",
        "tb", "tc", "td", "tf", "tg", "tj", "tk", "tl", "tm", "tn", "tp", "tq",
        "ts", "tt", "tv", "tw", "uc", "ue", "ug", "uh", "uj", "uk", "um", "up",
        "uq", "uu", "vb", "vc", "vd", "vf", "vg", "vj", "vk", "vl", "vm", "vn",
        "vp", "vq", "vr", "vs", "vt", "vv", "vw", "wb", "wc", "wd", "wf", "wg",
        "wj", "wk", "wl", "wm", "wn", "wp", "wq", "wr", "ws", "wt", "wv", "ww",
        "xc", "xd", "xf", "xg", "xj", "xk", "xl", "xm", "xn", "xp", "xq", "xr",
        "xs", "xt", "xv", "xw", "zc", "zd", "zf", "zg", "zj", "zk", "zl", "zm",
        "zn", "zp", "zq", "zr", "zs", "zt", "zv", "zw",
    };
    return s;
}

static const QSet<QString> &commonStartTrigrams()
{
    static const QSet<QString> s = {
        "the", "and", "for", "are", "but", "not", "you", "all", "can", "was",
        "wil", "may", "has", "had", "did", "thi", "tha", "wit", "fro", "hav",
        "bee", "mor", "som", "wor", "rea", "pro", "con", "com", "dis", "pre",
        "sub", "int", "ext", "var", "fun", "obj", "imp", "exp", "res", "ret",
        "def", "pub", "pri", "sta", "str", "sor", "fil", "tex", "dat", "num",
        "col", "row", "tab", "key", "val", "typ", "err", "msg", "log", "deb",
        "inf", "war", "fai", "tes", "ben", "hel", "abo", "add", "rem", "upd",
        "del", "get", "set", "put", "pos", "new", "old", "cur", "bes", "cas",
        "dec", "inc", "out", "ove", "und", "whe", "wha", "how", "why", "sha",
        "cou", "wou", "nee", "see", "kee", "mak", "tak", "giv", "fin", "len",
        "str", "cha", "flo", "boo", "dou", "sho", "byt", "dir", "pat", "nam",
        "siz", "kno", "def", "use", "wor", "lik", "loo", "doe", "let", "any",
        "eve", "man", "bot", "eac", "few", "mos", "oth", "nor", "onl", "sam",
        "ver", "jus", "bec", "asi", "uni", "usi", "wri", "cre", "mov", "cop",
        "ope", "clo", "bef", "aft", "bet", "bel", "dur", "alo", "aga", "fir",
        "las", "nex", "goo", "hig", "low", "big", "sma", "lon", "sho", "til",
        "nei", "ano",
    };
    return s;
}

static const QSet<QString> &keepAbbreviations()
{
    static const QSet<QString> s = {
        "ASR",   "OCR",  "LLM",   "UI",   "IDE",   "API", "SDK",   "JSON",
        "XML",   "YAML", "TOML",  "INI",  "YML",   "CMD", "EXE",   "DLL",
        "SO",    "ZIP",  "TAR",   "GZ",   "BZ2",   "GIF", "SVG",   "ICO",
        "PNG",   "JPG",  "JPEG",  "CSS",  "HTML",  "MD",  "TXT",   "UTF",
        "CRLF",  "HTTP", "HTTPS", "TCP",  "UDP",   "IP",  "DNS",   "SSL",
        "SSH",   "FTP",  "SFTP",  "SMB",  "WASM",  "CLI", "GUI",   "TUI",
        "RPC",   "REST", "URL",   "URI",  "URN",   "PID", "GID",   "UID",
        "UUID",  "GUID", "SHA",   "AES",  "RSA",   "CPU", "GPU",   "RAM",
        "ROM",   "SSD",  "HDD",   "USB",  "PCI",   "DMA", "BSD",   "MIT",
        "GPL",   "AGPL", "LGPL",  "MSVC", "MINGW", "GCC", "CLANG", "NINJA",
        "CMAKE", "QT",
    };
    return s;
}

// ── Unicode helpers ──

static bool isCjk(const QChar ch)
{
    return ch.unicode() >= 0x4E00 && ch.unicode() <= 0x9FFF;
}

// ── Normalization ──

static QString normalizeText(const QString &text)
{
    QString result = text.normalized(QString::NormalizationForm_KC);
    result.replace(QRegularExpression(QStringLiteral("\\s+")),
                   QStringLiteral(" "));

    // Consolidate CJK characters separated by spaces (common OCR artifact)
    // e.g., "正 在 读 取" -> "正在读取"
    QString out;
    out.reserve(result.size());
    int i = 0;
    while (i < result.size()) {
        if (isCjk(result[i])) {
            out += result[i];
            ++i;
            while (i < result.size()) {
                if (isCjk(result[i])) {
                    out += result[i];
                    ++i;
                }
                else if (result[i] == QLatin1Char(' ') &&
                         i + 1 < result.size() && isCjk(result[i + 1]))
                {
                    ++i; // skip the space
                }
                else {
                    break;
                }
            }
        }
        else {
            out += result[i];
            ++i;
        }
    }
    return out.trimmed();
}

// ── OCR artifact detection ──

static QString extractAlphaOnly(const QString &tok)
{
    QString result;
    result.reserve(tok.size());
    for (const QChar ch : tok) {
        if (ch.isLetter()) {
            result += ch;
        }
    }
    return result;
}

static bool isRarelyStartingWord(const QString &tok)
{
    const QString alpha = extractAlphaOnly(tok);
    if (alpha.size() < 3) {
        return false;
    }
    return rareStartBigrams().contains(alpha.left(2)) &&
           !commonStartTrigrams().contains(alpha.left(3));
}

static bool hasAbnormalMixedCase(const QString &tok)
{
    if (tok.isEmpty()) {
        return false;
    }

    // Check ASCII
    bool allAscii = true;
    for (const QChar ch : tok) {
        if (ch.unicode() > 127) {
            allAscii = false;
            break;
        }
    }
    if (!allAscii) {
        return false;
    }

    bool hasAlpha = false;
    for (const QChar ch : tok) {
        if (ch.isLetter()) {
            hasAlpha = true;
            break;
        }
    }
    if (!hasAlpha) {
        return false;
    }

    // Short tokens (3-4 chars): any internal uppercase -> suspicious
    if (tok.size() <= 4) {
        for (int i = 1; i < tok.size(); ++i) {
            if (tok[i].isUpper()) {
                return true;
            }
        }
        return false;
    }

    // Case 1: starts with lowercase, uppercase in first 4
    // e.g., cIeanedContext (c low, I up)
    if (tok[0].isLower()) {
        for (int i = 1; i < 4 && i < tok.size(); ++i) {
            if (tok[i].isUpper()) {
                return true;
            }
        }
    }

    // Case 2: leading uppercase segment of 3+ chars followed by lowercase
    // e.g., SYStem (SYS is 3 uppercase, then t)
    int leadingUpper = 0;
    for (const QChar ch : tok) {
        if (ch.isUpper()) {
            ++leadingUpper;
        }
        else {
            break;
        }
    }
    if (leadingUpper >= 3) {
        const QString prefix = tok.left(leadingUpper);
        if (!keepAbbreviations().contains(prefix)) {
            return true;
        }
    }

    // Case 3: multiple uppercase/lowercase transitions
    int transitions = 0;
    for (int i = 1; i < tok.size(); ++i) {
        if (tok[i].isLetter() && tok[i - 1].isLetter() &&
            tok[i].isUpper() != tok[i - 1].isUpper())
        {
            ++transitions;
        }
    }
    if (transitions >= 4) {
        return true;
    }

    return false;
}

static bool hasDigitSubstitution(const QString &tok)
{
    bool allAscii = true;
    int digits = 0;
    for (const QChar ch : tok) {
        if (ch.unicode() > 127) {
            allAscii = false;
            break;
        }
        if (ch.isDigit()) {
            ++digits;
        }
    }
    if (!allAscii || digits == 0) {
        return false;
    }

    // Has digit not at first position (e.g., Ta1kInput)
    for (int i = 1; i < tok.size(); ++i) {
        if (tok[i].isDigit()) {
            return true;
        }
    }
    return false;
}

static bool looksLikeOcrArtifact(const QString &tok)
{
    // All digits
    bool allDigit = true;
    for (const QChar ch : tok) {
        if (!ch.isDigit()) {
            allDigit = false;
            break;
        }
    }
    if (allDigit) {
        return true;
    }

    if (tok.size() <= 2) {
        return true;
    }
    if (hasAbnormalMixedCase(tok)) {
        return true;
    }
    if (hasDigitSubstitution(tok)) {
        return true;
    }

    // Lowercase-only fragment check
    bool lowercaseOnly = true;
    for (const QChar ch : tok) {
        if (ch.isLetter() && !ch.isLower()) {
            lowercaseOnly = false;
            break;
        }
    }
    if (lowercaseOnly && isRarelyStartingWord(tok)) {
        return true;
    }

    return false;
}

// ── File extensions pattern ──

static const QString &fileExtsPattern()
{
    static const QString p = QStringLiteral(
        "\\.(?:cpp|h|hpp|cxx|hxx|cc|hh|c|py|rs|js|ts|go|java|"
        "ui|qrc|pro|cmake|txt|md|json|yaml|yml|toml|ini|cfg|conf|"
        "png|jpg|jpeg|gif|svg|ico|xml|html|css|scss|less|"
        "exe|dll|so|dylib|lib|a|zip|tar|gz|bz2|7z|"
        "bat|sh|ps1|cmd)");
    return p;
}

// ── Token extraction ──

static QList<QPair<QString, QString>> extractTokens(const QString &text)
{
    QList<QPair<QString, QString>> tokens;

    // 1. File paths with extensions
    {
        static const QRegularExpression re(
            QStringLiteral("(?:^|[\\s,;:（）()\\[\\]{}<>\"'`])"
                           "(?:[\\w.~\\-]+[\\\\/])?"
                           "[\\w.~\\-]{3,}") +
            fileExtsPattern());
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            auto m = it.next();
            QString tok = m.captured();
            // Strip leading delimiter character
            int start = 0;
            for (int j = 0; j < tok.size(); ++j) {
                if (tok[j].isLetterOrNumber() || tok[j] == QLatin1Char('.') ||
                    tok[j] == QLatin1Char('~') || tok[j] == QLatin1Char('-') ||
                    tok[j] == QLatin1Char('_') || tok[j] == QLatin1Char('\\') ||
                    tok[j] == QLatin1Char('/'))
                {
                    start = j;
                    break;
                }
            }
            tok = tok.mid(start);
            if (!tok.isEmpty()) {
                tokens.append({QStringLiteral("path"), tok});
            }
        }
    }

    // 2. Reconstruct broken file names across OCR spaces
    // e.g., "voice_i nput_controller" -> "voice_input_controller"
    {
        static const QRegularExpression re(
            QStringLiteral("[\\w\\-~]+_[\\w\\-~]*\\s+"
                           "[\\w\\-~]*_[\\w\\-~]{2,}") +
            QStringLiteral("(?:") + fileExtsPattern() + QStringLiteral(")?"));
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            auto m = it.next();
            const QString tok = m.captured();
            const QString joined =
                QString(tok).replace(QLatin1Char(' '), QString());
            if (joined != tok && joined.contains(QLatin1Char('_'))) {
                tokens.append({QStringLiteral("ident"), joined});
            }
        }
    }

    // 3. PascalCase / camelCase / snake_case identifiers
    {
        static const QRegularExpression re(
            QStringLiteral("[A-Z][a-z]+(?:[A-Z][a-z]*)+"
                           "|[a-z]+_[a-z][\\w]*"
                           "|[a-z]+[A-Z][a-zA-Z]*"));
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            const QString tok = it.next().captured();
            if (tok.size() >= 4) {
                tokens.append({QStringLiteral("ident"), tok});
            }
        }
    }

    // 4. Version strings (e.g., v4.2, v3.11)
    {
        static const QRegularExpression re(
            QStringLiteral("\\bv\\d+(?:\\.\\d+)+"),
            QRegularExpression::CaseInsensitiveOption);
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            tokens.append({QStringLiteral("version"), it.next().captured()});
        }
    }

    // 5. General alphanumeric tokens >= 5 chars
    {
        static const QRegularExpression re(
            QStringLiteral("\\b[a-zA-Z][a-zA-Z0-9]{4,}\\b"));
        auto it = re.globalMatch(text);
        while (it.hasNext()) {
            tokens.append({QStringLiteral("word"), it.next().captured()});
        }
    }

    // 6. CJK sequences >= 2 chars (not stop words)
    {
        QString cjkSeq;
        for (const QChar ch : text) {
            if (isCjk(ch)) {
                cjkSeq += ch;
            }
            else {
                if (cjkSeq.size() >= 2 && !chineseStopWords().contains(cjkSeq))
                {
                    tokens.append({QStringLiteral("cjk"), cjkSeq});
                }
                cjkSeq.clear();
            }
        }
        if (cjkSeq.size() >= 2 && !chineseStopWords().contains(cjkSeq)) {
            tokens.append({QStringLiteral("cjk"), cjkSeq});
        }
    }

    return tokens;
}

// ── Main filter ──

static bool shouldKeep(const QString &category, const QString &tok)
{
    if (category == QStringLiteral("path")) {
        // Extract name part (everything before last extension dot)
        const int dotIdx = tok.lastIndexOf(QLatin1Char('.'));
        QString namePart = dotIdx >= 0 ? tok.left(dotIdx) : tok;
        // Strip directory prefix
        const int sepIdx = qMax(namePart.lastIndexOf(QLatin1Char('\\')),
                                namePart.lastIndexOf(QLatin1Char('/')));
        if (sepIdx >= 0) {
            namePart = namePart.mid(sepIdx + 1);
        }
        if (namePart.size() < 3) {
            return false;
        }
        if (looksLikeOcrArtifact(namePart)) {
            return false;
        }
        return true;
    }

    if (category == QStringLiteral("ident")) {
        return !looksLikeOcrArtifact(tok);
    }

    if (category == QStringLiteral("version")) {
        return true;
    }

    if (category == QStringLiteral("cjk")) {
        return true;
    }

    if (category == QStringLiteral("word")) {
        if (looksLikeOcrArtifact(tok)) {
            return false;
        }

        // Check if all ASCII
        bool allAscii = true;
        for (const QChar ch : tok) {
            if (ch.unicode() > 127) {
                allAscii = false;
                break;
            }
        }

        // Uppercase-starting proper noun
        if (allAscii && tok[0].isUpper()) {
            return true;
        }
        // Contains underscore
        if (tok.contains(QLatin1Char('_'))) {
            return true;
        }
        // All uppercase short abbreviation
        bool allUpper = true;
        for (const QChar ch : tok) {
            if (ch.isLetter() && !ch.isUpper()) {
                allUpper = false;
                break;
            }
        }
        if (allUpper && tok.size() <= 6) {
            return true;
        }
        // Lowercase token not in stop words
        if (tok.size() >= 5 && !englishStopWords().contains(tok.toLower())) {
            return true;
        }
        return false;
    }

    return false;
}

} // namespace

namespace talkinput
{

QString extractOcrWords(const QString &text)
{
    if (text.isEmpty()) {
        return {};
    }

    const QString normalized = normalizeText(text);
    const auto categorized = extractTokens(normalized);

    QStringList result;
    QSet<QString> seen;
    for (const auto &pair : categorized) {
        if (!shouldKeep(pair.first, pair.second)) {
            continue;
        }
        const QString key = pair.second.toLower();
        if (!seen.contains(key)) {
            seen.insert(key);
            result.append(pair.second);
        }
    }

    return result.join(QLatin1Char(' '));
}

} // namespace talkinput
