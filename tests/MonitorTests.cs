using System;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Windows.Forms;
using Microsoft.Win32;
namespace SwitchMonitor
{
    public static class MonitorTests
    {
        private static void Check(bool condition, string name) { if (!condition) throw new Exception(name); }
        [STAThread]
        public static int Main()
        {
            try { return RunTests(); }
            catch (Exception error) { Console.Error.WriteLine(error); return 1; }
        }
        private static int RunTests()
        {
            string ip;
            Check(Settings.NormalizeIp(" 192.168.1.131 ", out ip) && ip == "192.168.1.131", "IPv4 normalization");
            foreach (string invalid in new[] { "", "localhost", "127.1", "256.1.1.1", "1.1.1.-1", "1.2.3.4/24", "1.2.3.4;cmd", "1.2.3.+4" }) Check(!Settings.NormalizeIp(invalid, out ip), "Invalid IPv4: " + invalid);
            Snapshot gdb = Snapshot.From("192.168.1.131", false, false, false, true);
            Check(gdb.Connected && gdb.Application == "Application unknown" && gdb.Services.Length == 1, "GDB-only must be connected, app unknown");
            Check(Snapshot.From("x", false, true, false, false).Application == "Application unknown", "A game service port does not prove an application name");
            Check(Snapshot.From("x", false, false, true, false).Application == "Application unknown", "A lab service port does not prove an application name");
            Check(Snapshot.From("x", false, true, true, true).Services.Length == 3, "All available services are shown without guessing a name");
            Check(Snapshot.From("x", true, false, false, false).Connected, "Ping-only reachable");
            Snapshot offline = Snapshot.From("x", false, false, false, false);
            Check(!offline.Connected && offline.LastSeenUtc == null && offline.Services.Length == 0, "No stale online state");
            string path = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "monitor-test-" + Guid.NewGuid().ToString("N") + ".json");
            try
            {
                Check(Settings.Load(path).Language == "en", "Missing config defaults to English");
                File.WriteAllText(path, "{\"Ip\":\"10.0.0.7\",\"IntervalSeconds\":3,\"TimeoutMs\":200}");
                Check(Settings.Load(path).Language == "en", "Existing config without language defaults to English");
                Check(Settings.Load(path).ApplicationTokenFile == "", "Existing config keeps identity queries disabled");
                File.WriteAllText(path, "{\"Ip\":\"10.0.0.7\",\"IntervalSeconds\":3,\"TimeoutMs\":200,\"Language\":\"fr\"}");
                Check(Settings.Load(path).Language == "en" && Settings.Load(path).Ip == "10.0.0.7", "Unsupported language keeps other settings");
                JsonFiles.Write(path, new Settings { Ip = "10.0.0.7", IntervalSeconds = 3, TimeoutMs = 200, Language = "ru", ApplicationTokenFile = "data/application-token.txt" });
                Check(Settings.Load(path).Language == "ru", "Russian choice survives reload");
                Check(Settings.Load(path).ApplicationTokenFile == "data/application-token.txt", "Selected identity token path survives reload");
                JsonFiles.Write(path, new Settings { Ip = "10.0.0.7", IntervalSeconds = 3, TimeoutMs = 200, Language = "en" });
                Check(Settings.Load(path).Language == "en", "Switching back to English survives reload");
                Check(Settings.Load(path).Ip == "10.0.0.7", "Config round trip");
                JsonFiles.Write(path, new Settings { Ip = "10.0.0.8", IntervalSeconds = 4, TimeoutMs = 400 });
                Check(Settings.Load(path).Ip == "10.0.0.8", "Atomic replacement");
                File.WriteAllText(path, "{broken"); Check(Settings.Load(path).Ip == "192.168.1.131", "Malformed config fallback");
                JsonFiles.Write(path, gdb); Check(JsonFiles.Read<Snapshot>(path).Gdb, "Snapshot round trip");
            }
            finally { if (File.Exists(path)) File.Delete(path); }
            TcpListener listener = new TcpListener(IPAddress.Loopback, 0); listener.Start();
            int port = ((IPEndPoint)listener.LocalEndpoint).Port;
            try
            {
                Check(NetworkProbe.Port("127.0.0.1", port, 250), "Listening localhost port");
                using (TcpClient accepted = listener.AcceptTcpClient())
                {
                    accepted.ReceiveTimeout = 1000;
                    Check(accepted.GetStream().ReadByte() == -1, "Probe must send no application commands or token");
                }
            }
            finally { listener.Stop(); }
            Stopwatch clock = Stopwatch.StartNew();
            Check(!NetworkProbe.Port("127.0.0.1", port, 250), "Closed localhost port");
            Check(clock.ElapsedMilliseconds < 2000, "Bounded closed-port probe");
            CheckLanguageMenu();
            CheckStartupRegistration();
            ApplicationIdentityTests.Run();
            Console.WriteLine("PASS: IP, services, settings, languages, startup, zero-payload TCP probes and authenticated read-only application identity.");
            return 0;
        }
        private static void CheckStartupRegistration()
        {
            string keyPath = "Software\\SwitchMonitor.Tests\\" + Guid.NewGuid().ToString("N");
            const string exe = @"C:\Program Files\Switch Monitor\SwitchMonitor.exe";
            const string root = @"C:\Users\Sample User\Switch Monitor";
            var startup = new StartupRegistration(Registry.CurrentUser, keyPath, exe, root);
            try
            {
                Check(!startup.Enabled, "Startup is disabled by default");
                using (var key = Registry.CurrentUser.OpenSubKey(keyPath)) Check(key == null, "Checking default state does not create registry keys");
                startup.SetEnabled(false);
                using (var key = Registry.CurrentUser.OpenSubKey(keyPath)) Check(key == null, "Disabling an absent entry does not create registry keys");
                using (var key = Registry.CurrentUser.CreateSubKey(keyPath)) key.SetValue("Other application", "keep me");
                startup.SetEnabled(true);
                var restarted = new StartupRegistration(Registry.CurrentUser, keyPath, exe, root + "\\");
                Check(restarted.Enabled, "Startup choice survives a new instance and trailing directory slash");
                using (var key = Registry.CurrentUser.OpenSubKey(keyPath))
                {
                    string[] entries = Array.FindAll(key.GetValueNames(), name => name != "Other application");
                    Check(entries.Length == 1, "Exactly one startup entry");
                    Check((string)key.GetValue(entries[0]) == "\"C:\\Program Files\\Switch Monitor\\SwitchMonitor.exe\" --root \"C:\\Users\\Sample User\\Switch Monitor\"", "Paths with spaces are quoted without a shell");
                }
                var another = new StartupRegistration(Registry.CurrentUser, keyPath, exe, root + " Copy");
                another.SetEnabled(true);
                restarted.SetEnabled(false);
                Check(!startup.Enabled && another.Enabled, "Disabling one copy preserves another copy");
                another.SetEnabled(false);
                using (var key = Registry.CurrentUser.OpenSubKey(keyPath)) Check(key.GetValueNames().Length == 1 && (string)key.GetValue("Other application") == "keep me", "Unrelated startup entry is preserved");
            }
            finally { Registry.CurrentUser.DeleteSubKeyTree(keyPath, false); }
        }
        private static void CheckLanguageMenu()
        {
            string root = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "language-test-" + Guid.NewGuid().ToString("N"));
            string config = Path.Combine(root, "data", "config.json");
            string startupKey = "Software\\SwitchMonitor.Tests\\" + Guid.NewGuid().ToString("N");
            var registration = new StartupRegistration(Registry.CurrentUser, startupKey, Application.ExecutablePath, root);
            JsonFiles.Write(config, new Settings { Ip = "127.0.0.1" });
            try
            {
                using (TrayMonitor monitor = new TrayMonitor(root, registration))
                {
                    try
                    {
                        NotifyIcon tray = (NotifyIcon)typeof(TrayMonitor).GetField("tray", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(monitor);
                        ToolStripMenuItem language = tray.ContextMenuStrip.Items["language"] as ToolStripMenuItem;
                        Check(language != null, "Language selector is available");
                        ToolStripMenuItem startup = tray.ContextMenuStrip.Items["startup"] as ToolStripMenuItem;
                        Check(startup != null && startup.Text == "Start with Windows", "Startup option is available in English");
                        Check(!startup.Checked, "Startup menu defaults to off");
                        startup.PerformClick();
                        Check(startup.Checked && registration.Enabled, "Startup menu enables registration");
                        var english = (ToolStripMenuItem)language.DropDownItems["english"];
                        var russian = (ToolStripMenuItem)language.DropDownItems["russian"];
                        Check(english.Checked && !russian.Checked && language.Text == "Language", "English is selected initially");
                        var snapshot = Snapshot.From("127.0.0.1", true, true, true, true, new ApplicationIdentity { Protocol = 2, Name = "Example Game", Version = "1.2.3" });
                        typeof(TrayMonitor).GetField("last", BindingFlags.Instance | BindingFlags.NonPublic).SetValue(monitor, snapshot);
                        english.PerformClick();
                        ((ToolStripMenuItem)tray.ContextMenuStrip.Items["details"]).PerformClick();
                        Form details = (Form)typeof(TrayMonitor).GetField("details", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(monitor);
                        details.Hide();
                        TextBox detailsText = (TextBox)details.Controls[0];
                        Check(detailsText.Text.Contains("Console is reachable") && detailsText.Text.Contains("Example Game") && detailsText.Text.Contains("Version: 1.2.3"), "Details show the reported name and version in English");
                        russian.PerformClick();
                        Check(russian.Checked && !english.Checked && language.Text == "Язык", "Russian menu applies immediately");
                        Check(startup.Text == "Запускать при входе в Windows", "Startup option follows selected language");
                        Check(tray.ContextMenuStrip.Items["details"].Text == "Подробности…" && tray.Text.Contains("Связь есть"), "Menu and tooltip switch together");
                        Check(detailsText.Text.Contains("Консоль доступна по сети") && detailsText.Text.Contains("Example Game") && detailsText.Text.Contains("Версия: 1.2.3"), "Russian details preserve the reported application name");
                        Check(JsonFiles.Read<Snapshot>(Path.Combine(root, "data", "status.json")).Application == "Example Game", "Status JSON preserves the actual name");
                        Check(Settings.Load(config).Language == "ru", "Menu choice is saved");
                        var pause = (ToolStripMenuItem)typeof(TrayMonitor).GetField("pauseItem", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(monitor);
                        pause.PerformClick();
                        var identityMenu = (ToolStripMenuItem)tray.ContextMenuStrip.Items["identity"];
                        Check(identityMenu.Text == "Имя приложения" && !identityMenu.Checked, "Identity menu follows the selected language and defaults to disabled");
                        typeof(TrayMonitor).GetMethod("ChangeIdentityToken", BindingFlags.Instance | BindingFlags.NonPublic).Invoke(monitor, new object[] { "data/missing-token.txt" });
                        Check(identityMenu.Checked && Settings.Load(config).ApplicationTokenFile == "data/missing-token.txt", "Identity setup is saved");
                        Snapshot cleared = JsonFiles.Read<Snapshot>(Path.Combine(root, "data", "status.json"));
                        Check(cleared.Paused && cleared.ReportedApplication == null && cleared.ApplicationVersion == null && !cleared.ApplicationStatus && cleared.Application == "Application unknown", "Changing lookup clears published identity even while paused");
                        ((ToolStripMenuItem)identityMenu.DropDownItems["disableIdentity"]).PerformClick();
                        Check(!identityMenu.Checked && Settings.Load(config).ApplicationTokenFile == "" && !detailsText.Text.Contains("Example Game"), "Disabling lookup saves the choice and clears the previous name immediately");
                    }
                    finally { monitor.ExitThread(); }
                }
                using (TrayMonitor restarted = new TrayMonitor(root, registration))
                {
                    try
                    {
                        NotifyIcon tray = (NotifyIcon)typeof(TrayMonitor).GetField("tray", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(restarted);
                        var language = (ToolStripMenuItem)tray.ContextMenuStrip.Items["language"];
                        var startup = (ToolStripMenuItem)tray.ContextMenuStrip.Items["startup"];
                        Check(startup.Checked, "Restart restores enabled startup choice");
                        startup.PerformClick();
                        Check(!startup.Checked && !registration.Enabled, "Startup menu removes registration");
                        Check(language.Text == "Язык", "Restart restores Russian");
                        ((ToolStripMenuItem)language.DropDownItems["english"]).PerformClick();
                        Check(language.Text == "Language" && Settings.Load(config).Language == "en", "English can be selected and saved again");
                    }
                    finally { restarted.ExitThread(); }
                }
            }
            finally { Directory.Delete(root, true); Registry.CurrentUser.DeleteSubKeyTree(startupKey, false); }
        }
    }
}
