#pragma once
#include <QString>
namespace wf {
// Installation check for packaged builds (`wordforge --self-test report.txt`): exercises the
// deployed platform, image, print and spelling plugins, fonts, file round trips through a
// Unicode path and PDF output without showing a window. Returns 0 when every check passes.
int runSelfTest(const QString &reportPath);
} // namespace wf
