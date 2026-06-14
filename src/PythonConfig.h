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

#ifndef PYTHON_PLUGIN_PYTHON_CONFIG_H
#define PYTHON_PLUGIN_PYTHON_CONFIG_H

#include <QString>
#include <QStringList>
#include <QtGlobal>

#undef slots
#include <pybind11/pybind11.h>

class QProcess;
class QWidget;

/// Simple representation of a SemVer version
struct Version
{
    constexpr Version() = default;

    constexpr Version(uint16_t major_, uint16_t minor_, uint16_t patch_)
        : major(major_), minor(minor_), patch(patch_)
    {
    }

    explicit Version(const QString &versionStr);

    /// Checks whether the Python version number described by
    /// this instance is compatible with the Python version the plugin
    /// was compiled with.
    ///
    /// As explained in https://docs.python.org/3/c-api/stable.html#stable:
    /// CPython’s Application Binary Interface (ABI) is forward- and backwards-compatible
    /// across a minor release.
    /// So, code compiled for Python 3.10.0 will work on 3.10.8 and vice versa,
    /// but will need to be compiled separately for 3.9.x and 3.10.x.
    ///
    /// \return True if the version is compatible.
    bool isCompatibleWithCompiledVersion() const;

    bool isNull() const
    {
        return major == 0 && minor == 0 && patch == 0;
    };

    bool operator==(const Version &other) const;

    uint16_t major{0};
    uint16_t minor{0};
    uint16_t patch{0};
};

/// Python Version the plugin was compiled against
constexpr Version PythonVersion(PY_MAJOR_VERSION, PY_MINOR_VERSION, PY_MICRO_VERSION);

/// Path-configuration values resolved by querying an environment's interpreter.
struct ResolvedPythonPaths
{
    QString prefix{};
    QString execPrefix{};
    QString basePrefix{};
    QString baseExecPrefix{};
    QStringList moduleSearchPaths{};

    /// True if the environment's interpreter could be queried.
    bool isValid() const
    {
        return !moduleSearchPaths.isEmpty();
    }
};

class PythonConfig final
{
  public:
    enum class Type
    {
        Venv,
        Conda,
        System,
        Unknown
    };

    PythonConfig() = default;

    Type type() const
    {
        return m_type;
    }

    template <class ostream> friend ostream &operator<<(ostream &o, Type type)
    {
        switch (type)
        {
        case Type::Venv:
            o << "Venv";
            break;
        case Type::Conda:
            o << "Conda";
            break;
        case Type::System:
            o << "System";
            break;
        case Type::Unknown:
            o << "Unknown";
            break;
        }
        return o;
    }

    const QString &pythonHome() const
    {
        return m_pythonHome;
    }

    /// Returns the path to the Python interpreter executable of this
    /// environment (e.g. `<root>/bin/python` or `<root>/Scripts/python.exe`).
    QString pythonExecutable() const;

    /// Queries this environment's interpreter for the path-configuration values
    /// (prefixes and `sys.path`) needed to initialize the embedded interpreter.
    ///
    /// \return The resolved paths, or an invalid result (see
    ///         ResolvedPythonPaths::isValid) if the interpreter could not be
    ///         queried.
    ResolvedPythonPaths resolvePaths() const;

    /// Sets the necessary settings of the QProcess so that
    /// it uses the correct Python exe.
    void preparePythonProcess(QProcess &pythonProcess) const;

    /// Calls the python.exe of this environment / config
    /// to get its version.
    ///
    /// \return The version returned by the python process
    ///         If the python process failed for whatever reason
    ///         the version will be {0, 0, 0}
    Version getVersion() const;

    /// Does some basic validation (check is python executable exists
    /// and checks if its version is compatible) and displays a GUI with
    /// a message describing the error to the user.
    ///
    /// \param parent parent for the GUI to be displayed, can be nullptr
    /// \return true if the config passes the validation
    ///         (meaning no error where displayed to the user)
    bool validateAndDisplayErrors(QWidget *parent = nullptr) const;

    static bool IsInsideEnvironment();
    static PythonConfig fromContainingEnvironment();

    /// # On Windows:
    /// Initialize python home and python path
    /// corresponding to the environment to be used.
    ///
    /// # Other Platforms
    /// Does nothing, as we rely on the system's python to be properly installed
    void initDefault();
#if defined(Q_OS_WIN32) || defined(Q_OS_MACOS)
    /// Initialize the paths to point to where the Python
    /// environment was bundled on a Windows installation
    void initBundled();
#endif
    /// Initialize from the path to an environment.
    /// Will try to guess if the environment is a conda env
    /// or a python venv
    void initFromLocation(const QString &prefix);

    template <class ostream> friend ostream &operator<<(ostream &o, const PythonConfig &config)
    {
        o << "PythonConfig { type: " << config.m_type << ", home: '" << config.m_pythonHome << "'}";
        return o;
    }

  private:
    QString m_pythonHome{};
    Type m_type{Type::Unknown};
};
#endif // PYTHON_PLUGIN_PYTHON_CONFIG_H
