// Starting a program that outlives the shell (an app, the browser for a link): detached, and
// with none of the shell's open files. Without that it inherits them, the GPU's render node
// among them: a shell that has quit (restarted) then keeps its graphics memory, held by every
// app it started, until they quit too.
#pragma once

#include <QProcess>

namespace launch {
inline bool detached(const QString &program, const QStringList &args, const QString &workingDir = {})
{
    QProcess p;
    p.setProgram(program);
    p.setArguments(args);
    p.setWorkingDirectory(workingDir);
    p.setUnixProcessParameters(QProcess::UnixProcessFlag::CloseFileDescriptors
                               | QProcess::UnixProcessFlag::ResetSignalHandlers);
    return p.startDetached();
}
}
