using System;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Windows.Forms;
namespace SwitchMonitor
{
    public static class MonitorTests
    {
        private static void Check(bool condition, string name) { if (!condition) throw new Exception(name); }
        [STAThread]
        public static int Main()
        {
            string ip;
            Check(Settings.NormalizeIp(" 192.168.1.131 ", out ip) && ip == "192.168.1.131", "IPv4 normalization");
            foreach (string invalid in new[] { "", "localhost", "127.1", "256.1.1.1", "1.1.1.-1", "1.2.3.4/24", "1.2.3.4;cmd", "1.2.3.+4" }) Check(!Settings.NormalizeIp(invalid, out ip), "Invalid IPv4: " + invalid);
            Snapshot gdb = Snapshot.From("192.168.1.131", false, false, false, true);
            Check(gdb.Connected && gdb.Application == "Application unknown" && gdb.Services.Length == 1, "GDB-only must be connected, app unknown");
            Check(Snapshot.From("x", false, true, false, false).Application == "Game (TCP)", "Game service detection");
            Check(Snapshot.From("x", false, false, true, false).Application == "ARM lab (TCP)", "LAB detection");
            Check(Snapshot.From("x", false, true, true, true).Application.Contains("Game + ARM lab"), "Both services must be shown");
            Check(Snapshot.From("x", true, false, false, false).Connected, "Ping-only reachable");
            Snapshot offline = Snapshot.From("x", false, false, false, false);
            Check(!offline.Connected && offline.LastSeenUtc == null && offline.Services.Length == 0, "No stale online state");
            string path = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "monitor-test-" + Guid.NewGuid().ToString("N") + ".json");
            try
            {
                Check(Settings.Load(path).Language == "en", "Missing config defaults to English");
                File.WriteAllText(path, "{\"Ip\":\"10.0.0.7\",\"IntervalSeconds\":3,\"TimeoutMs\":200}");
                Check(Settings.Load(path).Language == "en", "Existing config without language defaults to English");
                File.WriteAllText(path, "{\"Ip\":\"10.0.0.7\",\"IntervalSeconds\":3,\"TimeoutMs\":200,\"Language\":\"fr\"}");
                Check(Settings.Load(path).Language == "en" && Settings.Load(path).Ip == "10.0.0.7", "Unsupported language keeps other settings");
                JsonFiles.Write(path, new Settings { Ip = "10.0.0.7", IntervalSeconds = 3, TimeoutMs = 200, Language = "ru" });
                Check(Settings.Load(path).Language == "ru", "Russian choice survives reload");
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
            Console.WriteLine("PASS: IPv4, service states, JSON, language defaults/persistence/live menu switching, localhost ports and zero transmitted payload.");
            return 0;
        }
        private static void CheckLanguageMenu()
        {
            string root = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "language-test-" + Guid.NewGuid().ToString("N"));
            string config = Path.Combine(root, "data", "config.json");
            JsonFiles.Write(config, new Settings { Ip = "127.0.0.1" });
            try
            {
                using (TrayMonitor monitor = new TrayMonitor(root))
                {
                    try
                    {
                        NotifyIcon tray = (NotifyIcon)typeof(TrayMonitor).GetField("tray", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(monitor);
                        ToolStripMenuItem language = tray.ContextMenuStrip.Items["language"] as ToolStripMenuItem;
                        Check(language != null, "Language selector is available");
                        var english = (ToolStripMenuItem)language.DropDownItems["english"];
                        var russian = (ToolStripMenuItem)language.DropDownItems["russian"];
                        Check(english.Checked && !russian.Checked && language.Text == "Language", "English is selected initially");
                        var snapshot = Snapshot.From("127.0.0.1", true, true, true, true);
                        typeof(TrayMonitor).GetField("last", BindingFlags.Instance | BindingFlags.NonPublic).SetValue(monitor, snapshot);
                        english.PerformClick();
                        ((ToolStripMenuItem)tray.ContextMenuStrip.Items["details"]).PerformClick();
                        Form details = (Form)typeof(TrayMonitor).GetField("details", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(monitor);
                        details.Hide();
                        TextBox detailsText = (TextBox)details.Controls[0];
                        Check(detailsText.Text.Contains("Console is reachable") && detailsText.Text.Contains("Game + ARM lab"), "Details use English initially");
                        russian.PerformClick();
                        Check(russian.Checked && !english.Checked && language.Text == "Язык", "Russian menu applies immediately");
                        Check(tray.ContextMenuStrip.Items["details"].Text == "Подробности…" && tray.Text.Contains("Связь есть"), "Menu and tooltip switch together");
                        Check(detailsText.Text.Contains("Консоль доступна по сети") && detailsText.Text.Contains("Игра + ARM lab"), "Open details switch to Russian immediately");
                        Check(JsonFiles.Read<Snapshot>(Path.Combine(root, "data", "status.json")).Application == "Game + ARM lab (TCP)", "Status JSON keeps stable English values");
                        Check(Settings.Load(config).Language == "ru", "Menu choice is saved");
                    }
                    finally { monitor.ExitThread(); }
                }
                using (TrayMonitor restarted = new TrayMonitor(root))
                {
                    try
                    {
                        NotifyIcon tray = (NotifyIcon)typeof(TrayMonitor).GetField("tray", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(restarted);
                        var language = (ToolStripMenuItem)tray.ContextMenuStrip.Items["language"];
                        Check(language.Text == "Язык", "Restart restores Russian");
                        ((ToolStripMenuItem)language.DropDownItems["english"]).PerformClick();
                        Check(language.Text == "Language" && Settings.Load(config).Language == "en", "English can be selected and saved again");
                    }
                    finally { restarted.ExitThread(); }
                }
            }
            finally { Directory.Delete(root, true); }
        }
    }
}
