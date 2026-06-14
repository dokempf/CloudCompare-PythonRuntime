// ##########################################################################
// #                                                                        #
// #                CLOUDCOMPARE PLUGIN: PythonRuntime                       #
// #                                                                        #
// #  This program is free software; you can redistribute it and/or modify  #
// #  it under the terms of the GNU General Public License as published by  #
// #  the Free Software Foundation; version 2 of the License.               #
// #                                                                        #
// #  This program is distributed in the hope that it will be useful,       #
// #  but WITHOUT ANY WARRANTY; without even the implied warranty of        #
// #  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         #
// #  GNU General Public License for more details.                          #
// #                                                                        #
// #                   COPYRIGHT: Thomas Montaigu                           #
// #                                                                        #
// ##########################################################################
#include "PythonConfig.h"
#include "Utilities.h"

#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QProcess>
#include <QtGlobal>

//================================================================================

Version::Version(const QString &versionStr) : Version()
{
    const QStringList parts = versionStr.split('.');
    if (parts.size() == 3)
    {
        major = parts[0].toUInt();
        minor = parts[1].toUInt();
        patch = parts[2].toUInt();
    }
}

bool Version::isCompatibleWithCompiledVersion() const
{
    return major == PythonVersion.major && minor == PythonVersion.minor;
}

bool Version::operator==(const Version &other) const
{
    return major == other.major && minor == other.minor && patch == other.patch;
}

static Version GetPythonExeVersion(QProcess &pythonProcess)
{
    pythonProcess.setArguments({"--version"});
    pythonProcess.start(QIODevice::ReadOnly);
    pythonProcess.waitForFinished();

    const QString versionStr = QString::fromUtf8(pythonProcess.readAllStandardOutput());

    const QStringList splits = versionStr.split(" ");
    if (splits.size() == 2 && splits[0].contains("Python"))
    {
        return Version(splits.at(1));
    }
    return Version{};
}

//================================================================================

static QString PathToPythonExecutableInEnv(PythonConfig::Type envType, const QString &envRoot)
{
#if defined(Q_OS_WINDOWS)
    switch (envType)
    {
    case PythonConfig::Type::Conda:
        return envRoot + "/python.exe";
    case PythonConfig::Type::Venv:
        return envRoot + "/Scripts/python.exe";
    case PythonConfig::Type::Unknown:
        return envRoot + "/python.exe";
    case PythonConfig::Type::System:
        return "python.exe";
    }
#else
    switch (envType)
    {
    case PythonConfig::Type::Conda:
    case PythonConfig::Type::Venv:
    case PythonConfig::Type::Unknown:
        return envRoot + "/bin/python";
    case PythonConfig::Type::System:
        return "python";
    }
#endif
    return {};
}

void PythonConfig::initDefault()
{
#if defined(Q_OS_WIN32) || defined(Q_OS_MACOS)
    initBundled();
#else
    // On Non windows platform
    // We do nothing, and rely on system's python installation
    m_type = Type::System;
#endif
}

#if defined(Q_OS_WIN32) || defined(Q_OS_MACOS)
void PythonConfig::initBundled()
{
#if defined(Q_OS_MACOS)
    const QString pythonEnvDirPath(QApplication::applicationDirPath() + "/../Resources/python");
#else
    const QString pythonEnvDirPath(QApplication::applicationDirPath() + "/plugins/Python");
#endif
    initFromLocation(pythonEnvDirPath);
}
#endif

void PythonConfig::initFromLocation(const QString &prefix)
{
    QDir envRoot(prefix);

    if (!envRoot.exists())
    {
        m_pythonHome = QString();
        m_type = Type::Unknown;
        return;
    }

    m_pythonHome = envRoot.path();

    if (envRoot.exists("pyvenv.cfg"))
    {
        m_type = Type::Venv;
    }
    else if (envRoot.exists("conda-meta"))
    {
        m_type = Type::Conda;
    }
    else
    {
        m_type = Type::Unknown;
    }
}

QString PythonConfig::pythonExecutable() const
{
    return PathToPythonExecutableInEnv(m_type, m_pythonHome);
}

ResolvedPythonPaths PythonConfig::resolvePaths() const
{
    QProcess pythonProcess;
    preparePythonProcess(pythonProcess);
    // -I runs the interpreter in isolated mode so the reported values match what
    // the embedded (also isolated) interpreter should use: standard library +
    // the environment's site-packages, without user-site or ambient PYTHON*.
    // The four prefixes are printed first (one per line), then the search paths.
    pythonProcess.setArguments({"-I",
                                "-c",
                                "import sys; "
                                "print(sys.prefix); print(sys.exec_prefix); "
                                "print(sys.base_prefix); print(sys.base_exec_prefix); "
                                "print(chr(10).join(sys.path))"});
    pythonProcess.start(QIODevice::ReadOnly);
    pythonProcess.waitForFinished();

    const QString output = QString::fromUtf8(pythonProcess.readAllStandardOutput());

    QStringList lines;
    for (const QString &line : output.split('\n'))
    {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
        {
            lines.append(trimmed);
        }
    }

    // 4 prefixes + at least one search path.
    if (lines.size() < 5)
    {
        plgWarning() << "Could not query Python paths from " << pythonExecutable() << ": got '"
                     << output << "'";
        return {};
    }

    ResolvedPythonPaths paths;
    paths.prefix = lines.takeFirst();
    paths.execPrefix = lines.takeFirst();
    paths.basePrefix = lines.takeFirst();
    paths.baseExecPrefix = lines.takeFirst();
    paths.moduleSearchPaths = lines;
    return paths;
}

void PythonConfig::preparePythonProcess(QProcess &pythonProcess) const
{
    pythonProcess.setProgram(pythonExecutable());

    // Conda env have SSL related libraries stored in a part that is not
    // in the path of the python exe, we have to add it ourselves.
    if (m_type == Type::Conda)
    {
        const QString additionalPath = QString("%1/Library/bin").arg(m_pythonHome);
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        QString path = env.value("PATH").append(QDir::listSeparator()).append(additionalPath);
        env.insert("PATH", path);
        pythonProcess.setProcessEnvironment(env);
    }
}

Version PythonConfig::getVersion() const
{
    QProcess pythonProcess;
    preparePythonProcess(pythonProcess);
    return GetPythonExeVersion(pythonProcess);
}

bool PythonConfig::validateAndDisplayErrors(QWidget *parent) const
{
    Version envVersion = getVersion();
    if (envVersion.isNull())
    {
        // This hints that the selected directory is likely not valid.
        QMessageBox::critical(
            parent,
            "Invalid Python Environment",
            "The selected directory does not seems to be a valid python environment");
        return false;
    }

    if (!envVersion.isCompatibleWithCompiledVersion())
    {
        QMessageBox::critical(
            parent,
            "Incompatible Python Environment",
            QString("The selected directory does not contain a Python Environment that is "
                    "compatible. Expected a python version like %1.%2.x, selected environment "
                    "has version %3.%4.%5")
                .arg(QString::number(PythonVersion.major),
                     QString::number(PythonVersion.minor),
                     QString::number(envVersion.major),
                     QString::number(envVersion.minor),
                     QString::number(envVersion.patch)));
        return false;
    }

    return true;
}

bool PythonConfig::IsInsideEnvironment()
{
    return qEnvironmentVariableIsSet("CONDA_PREFIX") || qEnvironmentVariableIsSet("VIRTUAL_ENV");
}

PythonConfig PythonConfig::fromContainingEnvironment()
{
    PythonConfig config;

    QString root = qEnvironmentVariable("CONDA_PREFIX");
    if (!root.isEmpty())
    {
        config.m_pythonHome = root;
        config.m_type = Type::Conda;
        return config;
    }

    root = qEnvironmentVariable("VIRTUAL_ENV");
    if (!root.isEmpty())
    {
        config.m_pythonHome = root;
        config.m_type = Type::Venv;
        return config;
    }

    return config;
}
