// launcher.cs — тонкий лончер DomMonitor.
//
// Собирается кнопкой build-exe.ps1 в DomMonitor.exe (подсистема Windows GUI:
// чёрного консольного окна нет). Сам ничего не рисует и в трее не висит:
// он только запускает DomMonitor-run.ps1 (супервизор), а окно и трей делает
// уже PowerShell-скрипт.
//
// Использование:
//   DomMonitor.exe            — запустить супервизор (без окон)
//   DomMonitor.exe --dry-run  — напечатать найденный путь к .ps1 и выйти
//
// Компиляция (C# 5, .NET Framework 4.x):
//   csc.exe /target:winexe /platform:anycpu /optimize+ /win32icon:app.ico ^
//           /out:DomMonitor.exe launcher.cs

using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace DomEnergyLauncher
{
    internal static class Program
    {
        private const string RunScriptName = "DomMonitor-run.ps1";

        // AttachConsole нужен только для --dry-run: у GUI-приложения своей
        // консоли нет, поэтому цепляемся к консоли родителя (если она есть),
        // чтобы напечатать путь. Новое окно при этом НЕ создаётся.
        private const uint ATTACH_PARENT_PROCESS = 0xFFFFFFFF;

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool AttachConsole(uint dwProcessId);

        [STAThread]
        private static int Main(string[] args)
        {
            bool dryRun = false;
            for (int i = 0; i < args.Length; i++)
            {
                if (string.Equals(args[i], "--dry-run", StringComparison.OrdinalIgnoreCase))
                {
                    dryRun = true;
                }
            }

            string scriptPath = FindRunScript();

            if (scriptPath == null)
            {
                string message =
                    "Не найден файл " + RunScriptName + "." + Environment.NewLine +
                    Environment.NewLine +
                    "Положите его рядом с DomMonitor.exe или в папку" + Environment.NewLine +
                    "%LOCALAPPDATA%\\DomMonitor\\" + Environment.NewLine +
                    Environment.NewLine +
                    "Проверенные места:" + Environment.NewLine +
                    "  " + Path.Combine(AppDomain.CurrentDomain.BaseDirectory, RunScriptName) + Environment.NewLine +
                    "  " + Path.Combine(GetLocalAppDataDir(), RunScriptName);

                if (dryRun)
                {
                    WriteToParentConsole("ERROR: " + RunScriptName + " not found");
                    WriteToParentConsole("searched: " + Path.Combine(AppDomain.CurrentDomain.BaseDirectory, RunScriptName));
                    WriteToParentConsole("searched: " + Path.Combine(GetLocalAppDataDir(), RunScriptName));
                }
                else
                {
                    MessageBox.Show(
                        message,
                        "Дом — энергия",
                        MessageBoxButtons.OK,
                        MessageBoxIcon.Error);
                }

                return 2;
            }

            if (dryRun)
            {
                WriteToParentConsole(scriptPath);
                return 0;
            }

            try
            {
                ProcessStartInfo psi = new ProcessStartInfo();
                psi.FileName = GetPowerShellPath();
                psi.Arguments =
                    "-NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File \"" +
                    scriptPath + "\"";
                psi.UseShellExecute = false;
                psi.CreateNoWindow = true;
                psi.WindowStyle = ProcessWindowStyle.Hidden;
                psi.WorkingDirectory = Path.GetDirectoryName(scriptPath);

                Process.Start(psi);
                return 0;
            }
            catch (Exception ex)
            {
                MessageBox.Show(
                    "Не удалось запустить " + RunScriptName + "." + Environment.NewLine +
                    Environment.NewLine +
                    ex.Message,
                    "Дом — энергия",
                    MessageBoxButtons.OK,
                    MessageBoxIcon.Error);
                return 1;
            }
        }

        // Ищем супервизор: сначала рядом с .exe, затем в %LOCALAPPDATA%\DomMonitor.
        private static string FindRunScript()
        {
            string nextToExe = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, RunScriptName);
            if (File.Exists(nextToExe))
            {
                return nextToExe;
            }

            string localAppData = Path.Combine(GetLocalAppDataDir(), RunScriptName);
            if (File.Exists(localAppData))
            {
                return localAppData;
            }

            return null;
        }

        private static string GetLocalAppDataDir()
        {
            return Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "DomMonitor");
        }

        // Полный путь к Windows PowerShell 5.1. Если его нет — пробуем "powershell.exe"
        // из PATH (например, на нестандартной установке).
        private static string GetPowerShellPath()
        {
            string windows = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
            string full = Path.Combine(
                windows,
                @"System32\WindowsPowerShell\v1.0\powershell.exe");

            if (File.Exists(full))
            {
                return full;
            }

            return "powershell.exe";
        }

        // Печатаем в консоль родителя. Если консоли нет (запуск из проводника),
        // молча ничего не печатаем — окно показывать не нужно.
        private static void WriteToParentConsole(string text)
        {
            try
            {
                AttachConsole(ATTACH_PARENT_PROCESS);
                Stream stdout = Console.OpenStandardOutput();
                using (StreamWriter writer = new StreamWriter(stdout))
                {
                    writer.WriteLine(text);
                    writer.Flush();
                }
            }
            catch
            {
                // Консоли нет — это нормально для GUI-запуска.
            }
        }
    }
}
