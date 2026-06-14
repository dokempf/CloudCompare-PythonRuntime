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

#include "PythonInterpreter.h"
#include "PythonStdErrOutRedirect.h"
#include "Runtime/Consoles.h"
#include "Utilities.h"

#include <pybind11/embed.h>

#include <memory>

#include <QApplication>
#include <QDir>
#include <QListWidgetItem>
#include <QMessageBox>

#include <ccLog.h>

#ifdef Q_OS_LINUX
#include <cstdio>
#include <dlfcn.h>
#endif

// seems like gcc defines macro with these names
#undef major
#undef minor

namespace py = pybind11;

namespace
{
/// Formats a Python exception with its full cause/context chain, the way Python
/// prints it to stderr. pybind11's `error_already_set::what()` only reports the
/// leaf exception, which hides the original cause - for example a missing
/// dependency behind a generic "ImportError: initialization failed".
QString FormatPythonException(const py::error_already_set &error)
{
    try
    {
        const py::gil_scoped_acquire gil;
        const py::module_ traceback = py::module_::import("traceback");
        const py::list lines =
            traceback.attr("format_exception")(error.type(), error.value(), error.trace());

        QString formatted;
        for (const py::handle line : lines)
        {
            formatted += QString::fromStdString(line.cast<std::string>());
        }
        return formatted.trimmed();
    }
    catch (const std::exception &)
    {
        // Fall back to the leaf message if formatting itself fails.
        return QString::fromUtf8(error.what());
    }
}

/// Returns the message to display for a caught exception, expanding Python
/// exceptions into their full traceback.
QString ExceptionText(const std::exception &error)
{
    if (const auto *pythonError = dynamic_cast<const py::error_already_set *>(&error))
    {
        return FormatPythonException(*pythonError);
    }
    return QString::fromUtf8(error.what());
}

/// cccorelib and pycc bind NumPy and import it while initializing their own
/// modules. When the selected environment lacks NumPy those imports fail with an
/// opaque "initialization failed" error, so detect it up front and tell the user
/// how to fix it.
void WarnIfNumPyIsMissing(const PythonConfig &config)
{
    try
    {
        const py::module_ importlibUtil = py::module_::import("importlib.util");
        if (!importlibUtil.attr("find_spec")("numpy").is_none())
        {
            return;
        }
    }
    catch (const py::error_already_set &)
    {
        // Fall through and warn: if NumPy cannot even be looked up, importing the
        // wrappers will not work either.
    }

    plgWarning() << "NumPy was not found in the selected Python environment. The 'cccorelib' "
                    "and 'pycc' modules require it and will fail to import. Install it with: "
                 << config.pythonExecutable() << " -m pip install numpy";
}
} // namespace

static py::dict CreateGlobals()
{
    py::dict globals;
    globals["__name__"] = "__main__";
    // TODO Someday we should require pybind11 > 2.6 and use py::detail::ensure_builtins_in_globals
    // ?
    globals["__builtins__"] = PyEval_GetBuiltins();
    return globals;
}

PythonInterpreter::State::State() : globals(CreateGlobals()), locals() {};

//================================================================================

PythonInterpreter::PythonInterpreter(QObject *parent) : QObject(parent) {}

bool PythonInterpreter::executeFile(const std::string &filepath)
{
    if (m_isExecuting)
    {
        return false;
    }
    Q_EMIT executionStarted();

    bool success{true};
    try
    {
        PyStdErrOutStreamRedirect guard;
        py::dict globals = CreateGlobals();
        globals["__file__"] = filepath;
        py::eval_file(filepath, globals);
    }
    catch (const std::exception &e)
    {
        ccLog::Warning(ExceptionText(e));
        success = false;
    }

    Q_EMIT executionFinished();
    return success;
}

template <pybind11::eval_mode mode>
void PythonInterpreter::executeCodeString(const std::string &code,
                                          QListWidget *output,
                                          State &state)
{
    if (m_isExecuting)
    {
        return;
    }

    Q_EMIT executionStarted();
    m_isExecuting = true;
    constexpr QColor orange(255, 100, 0);

    try
    {
        if (output != nullptr)
        {
            constexpr auto movePolicy = py::return_value_policy::move;
            py::object newStdout = py::cast(ListWidgetConsole(output), movePolicy);
            py::object newStderr = py::cast(ListWidgetConsole(output, orange), movePolicy);
            PyStdErrOutStreamRedirect redirect{std::move(newStdout), std::move(newStderr)};
            py::eval<mode>(code, state.globals, state.locals);
        }
        else
        {
            PyStdErrOutStreamRedirect redirect;
            py::eval<mode>(code, state.globals, state.locals);
        }
    }
    catch (const std::exception &e)
    {
        const QString text = ExceptionText(e);
        if (output)
        {
            auto message = new QListWidgetItem(text);
            message->setForeground(Qt::red);
            output->addItem(message);
        }
        else
        {
            ccLog::Error(text);
        }
    }

    m_isExecuting = false;
    Q_EMIT executionFinished();
}

void PythonInterpreter::executeCodeWithState(const std::string &code,
                                             QListWidget *output,
                                             State &state)
{
    executeCodeString<py::eval_mode::eval_statements>(code, output, state);
}

void PythonInterpreter::executeStatementWithState(const std::string &code,
                                                  QListWidget *output,
                                                  State &state)
{
    executeCodeString<py::eval_mode::eval_single_statement>(code, output, state);
}

void PythonInterpreter::executeCode(const std::string &code, QListWidget *output)
{
    if (m_isExecuting)
    {
        return;
    }
    State tmpState;
    executeCodeWithState(code, output, tmpState);
}

void PythonInterpreter::executeFunction(const pybind11::object &function)
{
    if (m_isExecuting)
    {
        return;
    }

    m_isExecuting = true;
    Q_EMIT executionStarted();
    try
    {
        py::gil_scoped_acquire scopedGil;
        PyStdErrOutStreamRedirect scopedRedirect;
        function();
    }
    catch (const std::exception &e)
    {
        ccLog::Error(QStringLiteral("Failed to start Python actions: %1").arg(ExceptionText(e)));
    }
    m_isExecuting = false;
    Q_EMIT executionFinished();
}

void PythonInterpreter::initialize(const PythonConfig &config)
{
    plgVerbose() << "Initializing the interpreter with: " << config;

#ifdef Q_OS_LINUX
    // Work-around issue: undefined symbol: PyExc_RecursionError
    // when trying to import numpy in the intepreter
    // e.g: https://github.com/numpy/numpy/issues/14946
    // https://stackoverflow.com/questions/49784583/numpy-import-fails-on-multiarray-extension-library-when-called-from-embedded-pyt
    // This workaround is weak

    const auto displaydlopenError = []()
    {
        char *error = dlerror();
        if (error)
        {
            plgWarning() << "dlopen error: " << error;
        }
    };

    char soName[25];
    snprintf(soName, 24, "libpython%d.%d.so", PythonVersion.major, PythonVersion.minor);
    m_libPythonHandle = dlopen(soName, RTLD_LAZY | RTLD_GLOBAL);
    if (!m_libPythonHandle)
    {
        displaydlopenError();
        snprintf(soName, 24, "libpython%d.%dm.so", PythonVersion.major, PythonVersion.minor);
        m_libPythonHandle = dlopen(soName, RTLD_LAZY | RTLD_GLOBAL);
        if (!m_libPythonHandle)
        {
            displaydlopenError();
        }
    }
#endif
    if (config.type() != PythonConfig::Type::System)
    {
        // We use PEP 0587 to init the interpreter.
        // The changes introduced in this PEP allows to handle the error
        // when the interpreter could not be initialized.
        //
        // Before that the python interpreter would simply exit the program
        // and that could be a bad user experience
        //
        // https://www.python.org/dev/peps/pep-0587/
        // https://docs.python.org/3/c-api/init_config.html#init-python-config
        const ResolvedPythonPaths paths = config.resolvePaths();
        if (!paths.isValid())
        {
            throw std::runtime_error(
                "Failed to query the environment's Python interpreter for its configuration");
        }

        PyConfig pyConfig;
        PyConfig_InitPythonConfig(&pyConfig);
        pyConfig.isolated = 1;

        const auto check = [&pyConfig](PyStatus status)
        {
            if (PyStatus_Exception(status))
            {
                const std::string message = status.err_msg ? status.err_msg : "unknown error";
                PyConfig_Clear(&pyConfig);
                throw std::runtime_error(message);
            }
        };

        const auto setString = [&](wchar_t **field, const QString &value)
        {
            const std::unique_ptr<wchar_t[]> wide(QStringToWcharArray(value));
            check(PyConfig_SetString(&pyConfig, field, wide.get()));
        };

        setString(&pyConfig.prefix, paths.prefix);
        setString(&pyConfig.exec_prefix, paths.execPrefix);
        setString(&pyConfig.base_prefix, paths.basePrefix);
        setString(&pyConfig.base_exec_prefix, paths.baseExecPrefix);
        setString(&pyConfig.executable, config.pythonExecutable());

        pyConfig.module_search_paths_set = 1;
        for (const QString &path : paths.moduleSearchPaths)
        {
            const std::unique_ptr<wchar_t[]> wide(QStringToWcharArray(path));
            check(PyWideStringList_Append(&pyConfig.module_search_paths, wide.get()));
        }

        check(PyConfig_Read(&pyConfig));
        check(Py_InitializeFromConfig(&pyConfig));
    }
    else
    {
        Py_Initialize();
    }

    // Make sure this module is imported
    // so that we can later easily construct our consoles.
    py::module::import("ccinternals");

#if !defined(USE_EMBEDDED_MODULES) && defined(Q_OS_WINDOWS)
    // In non-embedded-modules builds the CloudCompare Python wrappers (pycc,
    // cccorelib) are installed next to the application rather than inside the
    // selected environment, so make that location importable.
    const QString bundledSitePackages =
        QApplication::applicationDirPath() + "/plugins/Python/Lib/site-packages";
    py::module::import("sys").attr("path").attr("append")(bundledSitePackages.toStdString());
#endif

    WarnIfNumPyIsMissing(config);
}

bool PythonInterpreter::IsInitialized()
{
    return Py_IsInitialized();
}

void PythonInterpreter::finalize()
{
    if (Py_IsInitialized())
    {
        py::finalize_interpreter();
#ifdef Q_OS_LINUX
        if (m_libPythonHandle)
        {
            dlclose(m_libPythonHandle);
            m_libPythonHandle = nullptr;
        }
#else
        Q_UNUSED(this);
#endif
    }
}

bool PythonInterpreter::isExecuting() const
{
    return m_isExecuting;
}
