using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Security.Cryptography;
using System.Security;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;
using Microsoft.Win32;

namespace SwitchMonitor
{
    [DataContract]
    public sealed class Settings
    {
        [DataMember] public string Ip = "192.168.1.131";
        [DataMember] public int IntervalSeconds = 5;
        [DataMember] public int TimeoutMs = 700;
        [DataMember] public string Language = "en";

        public static bool NormalizeIp(string text, out string value)
        {
            value = null;
            if (text == null) return false;
            string[] parts = text.Trim().Split('.');
            if (parts.Length != 4) return false;
            byte[] bytes = new byte[4];
            for (int i = 0; i < 4; ++i)
                if (!byte.TryParse(parts[i], NumberStyles.None, CultureInfo.InvariantCulture, out bytes[i])) return false;
            value = new IPAddress(bytes).ToString();
            return true;
        }
        public static Settings Load(string path)
        {
            try
            {
                Settings s = JsonFiles.Read<Settings>(path);
                string ip;
                if (s == null || !NormalizeIp(s.Ip, out ip) || s.IntervalSeconds < 2 || s.IntervalSeconds > 60 || s.TimeoutMs < 100 || s.TimeoutMs > 3000) return new Settings();
                s.Ip = ip; s.Language = UiText.NormalizeLanguage(s.Language); return s;
            }
            catch (Exception) { return new Settings(); }
        }
    }

    public sealed class UiText
    {
        private readonly bool russian;
        public static string NormalizeLanguage(string language) { return string.Equals(language, "ru", StringComparison.OrdinalIgnoreCase) ? "ru" : "en"; }
        public UiText(string language) { russian = NormalizeLanguage(language) == "ru"; }
        private static readonly Dictionary<string, string[]> messages = new Dictionary<string, string[]>
        {
            { "Language", new[] { "Language", "Язык" } },
            { "StartWithWindows", new[] { "Start with Windows", "Запускать при входе в Windows" } },
            { "StartupSettingError", new[] { "Could not update Windows startup. Check access to your user registry.", "Не удалось изменить автозапуск Windows. Проверьте доступ к реестру пользователя." } },
            { "Details", new[] { "Details…", "Подробности…" } },
            { "ChangeIp", new[] { "Change IP…", "Изменить IP…" } },
            { "Probe", new[] { "Check now", "Проверить сейчас" } },
            { "Pause", new[] { "Pause", "Пауза" } },
            { "Exit", new[] { "Exit", "Выход" } },
            { "Checking", new[] { "Checking…", "Проверка…" } },
            { "Connected", new[] { "Connected", "Связь есть" } },
            { "NoResponse", new[] { "No response", "Нет ответа" } },
            { "Application", new[] { "Application", "Приложение" } },
            { "Services", new[] { "Services", "Службы" } },
            { "Checked", new[] { "Checked", "Проверено" } },
            { "LastSeen", new[] { "Last response", "Последний ответ" } },
            { "Game", new[] { "Game (TCP)", "Игра (TCP)" } },
            { "GameLab", new[] { "Game + ARM lab (TCP)", "Игра + ARM lab (TCP)" } },
            { "Unknown", new[] { "Application unknown", "Приложение не определено" } },
            { "GameService", new[] { "Game TCP :28281", "Игра TCP :28281" } },
            { "Paused", new[] { "Monitoring paused", "Мониторинг на паузе" } },
            { "Available", new[] { "Console is reachable", "Консоль доступна по сети" } },
            { "Unavailable", new[] { "No response from console", "Нет ответа от консоли" } },
            { "NotFound", new[] { "none found", "не найдены" } },
            { "Yes", new[] { "yes", "да" } },
            { "No", new[] { "no", "нет" } },
            { "ProbeNote", new[] { "TCP indicates port availability, not FPS or responsiveness.\r\nNetloader is not probed.", "TCP показывает доступность порта, а не FPS или отсутствие зависаний.\r\nNetloader не опрашивается." } },
            { "ConsoleIp", new[] { "Console IP", "IP консоли" } },
            { "Save", new[] { "Save", "Сохранить" } },
            { "Cancel", new[] { "Cancel", "Отмена" } },
            { "InvalidIp", new[] { "Enter an IPv4 address, for example 192.168.1.131.", "Введите IPv4, например 192.168.1.131." } },
            { "SaveError", new[] { "Could not save settings. Check write access to the data folder.", "Не удалось сохранить настройки. Проверьте права записи в папку data." } },
            { "StartupError", new[] { "Could not start Switch Monitor. Check write access to the data folder.", "Не удалось запустить Switch Monitor. Проверьте права записи в папку data." } }
        };
        public string this[string key] { get { return messages[key][russian ? 1 : 0]; } }
        public string ApplicationName(Snapshot snapshot) { return snapshot.Game && snapshot.ArmLab ? this["GameLab"] : snapshot.Game ? this["Game"] : snapshot.ArmLab ? "ARM lab (TCP)" : this["Unknown"]; }
        public string[] ServiceNames(Snapshot snapshot)
        {
            var result = new List<string>();
            if (snapshot.Game) result.Add(this["GameService"]);
            if (snapshot.ArmLab) result.Add("ARM lab TCP :28284");
            if (snapshot.Gdb) result.Add("GDB TCP :22225");
            return result.ToArray();
        }
    }

    public sealed class StartupRegistration
    {
        private const string RunKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
        private readonly RegistryKey hive;
        private readonly string keyPath, valueName, command;

        public StartupRegistration(string root)
            : this(Registry.CurrentUser, RunKey, Application.ExecutablePath, root) { }

        public StartupRegistration(RegistryKey hive, string keyPath, string executable, string root)
        {
            this.hive = hive; this.keyPath = keyPath;
            string fullRoot = Path.GetFullPath(root);
            string volume = Path.GetPathRoot(fullRoot);
            fullRoot = fullRoot.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            if (fullRoot.Length < volume.Length) fullRoot = volume;
            using (SHA256 hash = SHA256.Create())
                valueName = "SwitchMonitor_" + BitConverter.ToString(hash.ComputeHash(Encoding.UTF8.GetBytes(fullRoot.ToUpperInvariant()))).Replace("-", "").Substring(0, 24);
            command = QuotePath(Path.GetFullPath(executable)) + " --root " + QuotePath(fullRoot);
        }

        private static string QuotePath(string path)
        {
            int trailing = 0;
            for (int i = path.Length - 1; i >= 0 && path[i] == '\\'; --i) ++trailing;
            return "\"" + path + new string('\\', trailing) + "\"";
        }

        public bool Enabled
        {
            get
            {
                using (RegistryKey key = hive.OpenSubKey(keyPath, false))
                    return key != null && string.Equals(key.GetValue(valueName) as string, command, StringComparison.OrdinalIgnoreCase);
            }
        }

        public void SetEnabled(bool enabled)
        {
            if (enabled)
            {
                using (RegistryKey key = hive.CreateSubKey(keyPath)) key.SetValue(valueName, command, RegistryValueKind.String);
            }
            else
            {
                using (RegistryKey key = hive.OpenSubKey(keyPath, true))
                    if (key != null) key.DeleteValue(valueName, false);
            }
        }
    }

    public static class JsonFiles
    {
        public static T Read<T>(string path)
        {
            using (FileStream f = File.OpenRead(path)) return (T)new DataContractJsonSerializer(typeof(T)).ReadObject(f);
        }
        public static void Write<T>(string path, T data)
        {
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(path)));
            string temp = path + ".tmp";
            using (FileStream f = File.Create(temp)) new DataContractJsonSerializer(typeof(T)).WriteObject(f, data);
            if (File.Exists(path)) File.Replace(temp, path, null);
            else File.Move(temp, path);
        }
    }

    [DataContract]
    public sealed class Snapshot
    {
        [DataMember] public string Ip;
        [DataMember] public bool Connected;
        [DataMember] public bool Ping;
        [DataMember] public bool Game;
        [DataMember] public bool ArmLab;
        [DataMember] public bool Gdb;
        [DataMember] public bool Paused;
        [DataMember] public string Application;
        [DataMember] public string CheckedUtc;
        [DataMember] public string LastSeenUtc;
        [DataMember] public long ProbeMs;
        [DataMember] public int MonitorPid;
        [DataMember] public string[] Services;

        public static Snapshot From(string ip, bool ping, bool game, bool lab, bool gdb)
        {
            bool connected = ping || game || lab || gdb;
            string now = DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture);
            var snapshot = new Snapshot { Ip = ip, Ping = ping, Game = game, ArmLab = lab, Gdb = gdb, Connected = connected, CheckedUtc = now, LastSeenUtc = connected ? now : null, MonitorPid = Process.GetCurrentProcess().Id };
            var text = new UiText("en");
            snapshot.Application = text.ApplicationName(snapshot); snapshot.Services = text.ServiceNames(snapshot);
            return snapshot;
        }
    }

    public static class NetworkProbe
    {
        public static bool Port(string ip, int port, int timeout)
        {
            try
            {
                using (TcpClient c = new TcpClient(AddressFamily.InterNetwork))
                {
                    IAsyncResult pending = c.BeginConnect(ip, port, null, null);
                    using (WaitHandle wait = pending.AsyncWaitHandle)
                    {
                        if (!wait.WaitOne(timeout)) return false;
                        c.EndConnect(pending);
                        return c.Connected;
                    }
                }
            }
            catch (Exception) { return false; }
        }
        private static bool Icmp(string ip, int timeout)
        {
            try { using (Ping p = new Ping()) return p.Send(ip, timeout).Status == IPStatus.Success; }
            catch (Exception) { return false; }
        }
        public static Snapshot Read(string ip, int timeout)
        {
            var clock = Stopwatch.StartNew();
            Task<bool> ping = Task.Run(() => Icmp(ip, timeout));
            Task<bool> game = Task.Run(() => Port(ip, 28281, timeout));
            Task<bool> lab = Task.Run(() => Port(ip, 28284, timeout));
            Task<bool> gdb = Task.Run(() => Port(ip, 22225, timeout));
            Task.WaitAll(ping, game, lab, gdb);
            Snapshot s = Snapshot.From(ip, ping.Result, game.Result, lab.Result, gdb.Result);
            s.ProbeMs = clock.ElapsedMilliseconds;
            return s;
        }
    }

    public sealed class TrayMonitor : ApplicationContext
    {
        [DllImport("user32.dll")] private static extern bool DestroyIcon(IntPtr handle);
        private readonly NotifyIcon tray = new NotifyIcon();
        private readonly Control dispatcher = new Control();
        private readonly System.Windows.Forms.Timer timer = new System.Windows.Forms.Timer();
        private readonly string configPath, statusPath;
        private readonly Icon green, red, gray;
        private readonly ToolStripMenuItem target = new ToolStripMenuItem();
        private readonly ToolStripMenuItem connection = new ToolStripMenuItem();
        private readonly ToolStripMenuItem application = new ToolStripMenuItem();
        private readonly ToolStripMenuItem services = new ToolStripMenuItem();
        private readonly ToolStripMenuItem updated = new ToolStripMenuItem();
        private readonly ToolStripMenuItem detailsItem = new ToolStripMenuItem { Name = "details" };
        private readonly ToolStripMenuItem changeIpItem = new ToolStripMenuItem();
        private readonly ToolStripMenuItem probeItem = new ToolStripMenuItem();
        private readonly ToolStripMenuItem pauseItem = new ToolStripMenuItem { CheckOnClick = true };
        private readonly ToolStripMenuItem languageItem = new ToolStripMenuItem { Name = "language" };
        private readonly ToolStripMenuItem englishItem = new ToolStripMenuItem("English") { Name = "english" };
        private readonly ToolStripMenuItem russianItem = new ToolStripMenuItem("Русский") { Name = "russian" };
        private readonly ToolStripMenuItem startupItem = new ToolStripMenuItem { Name = "startup" };
        private readonly ToolStripMenuItem exitItem = new ToolStripMenuItem();
        private readonly StartupRegistration startup;
        private Settings settings;
        private UiText text;
        private Snapshot last;
        private int busy;
        private bool paused;
        private volatile bool stopped;
        private Form details;
        private TextBox detailsText;

        private static Icon Dot(Color color)
        {
            using (Bitmap bitmap = new Bitmap(32, 32))
            using (Graphics g = Graphics.FromImage(bitmap))
            using (SolidBrush brush = new SolidBrush(color))
            using (Pen border = new Pen(Color.FromArgb(230, 240, 240, 240), 2))
            {
                g.Clear(Color.Transparent); g.SmoothingMode = SmoothingMode.AntiAlias;
                g.FillEllipse(brush, 3, 3, 26, 26); g.DrawEllipse(border, 3, 3, 26, 26);
                IntPtr handle = bitmap.GetHicon();
                try { using (Icon borrowed = Icon.FromHandle(handle)) return (Icon)borrowed.Clone(); }
                finally { DestroyIcon(handle); }
            }
        }
        public TrayMonitor(string root) : this(root, new StartupRegistration(root)) { }
        public TrayMonitor(string root, StartupRegistration startup)
        {
            this.startup = startup;
            string data = Path.Combine(root, "data");
            Directory.CreateDirectory(data);
            configPath = Path.Combine(data, "config.json"); statusPath = Path.Combine(data, "status.json");
            settings = Settings.Load(configPath); JsonFiles.Write(configPath, settings);
            text = new UiText(settings.Language);
            green = Dot(Color.FromArgb(28, 184, 89)); red = Dot(Color.FromArgb(230, 60, 62)); gray = Dot(Color.Gray);
            IntPtr unused = dispatcher.Handle;
            var menu = new ContextMenuStrip();
            foreach (ToolStripMenuItem item in new[] { target, connection, application, services, updated }) { item.Enabled = false; menu.Items.Add(item); }
            menu.Items.Add(new ToolStripSeparator());
            detailsItem.Click += (sender, args) => ShowDetails();
            changeIpItem.Click += (sender, args) => ChangeIp();
            probeItem.Click += (sender, args) => BeginRound();
            pauseItem.CheckedChanged += (sender, args) => { paused = pauseItem.Checked; Render(); if (!paused) BeginRound(); };
            englishItem.Click += (sender, args) => ChangeLanguage("en");
            russianItem.Click += (sender, args) => ChangeLanguage("ru");
            startupItem.Click += (sender, args) => ChangeStartup();
            languageItem.DropDownItems.AddRange(new ToolStripItem[] { englishItem, russianItem });
            menu.Items.AddRange(new ToolStripItem[] { detailsItem, changeIpItem, probeItem, pauseItem, languageItem, startupItem, new ToolStripSeparator(), exitItem });
            exitItem.Click += (sender, args) => ExitThread();
            tray.ContextMenuStrip = menu; tray.Icon = gray; tray.Visible = true;
            tray.DoubleClick += (sender, args) => ShowDetails();
            timer.Interval = settings.IntervalSeconds * 1000;
            timer.Tick += (sender, args) => BeginRound(); timer.Start();
            Render(); BeginRound();
        }
        private void BeginRound()
        {
            if (paused || stopped || Interlocked.CompareExchange(ref busy, 1, 0) != 0) return;
            string ip = settings.Ip; int timeout = settings.TimeoutMs;
            Task.Run(() => NetworkProbe.Read(ip, timeout)).ContinueWith(task =>
            {
                Interlocked.Exchange(ref busy, 0);
                if (stopped) return;
                Snapshot result = task.Status == TaskStatus.RanToCompletion ? task.Result : Snapshot.From(ip, false, false, false, false);
                try { dispatcher.BeginInvoke((Action)(() =>
                {
                    if (stopped || ip != settings.Ip) return;
                    if (!result.Connected && last != null && last.Ip == ip) result.LastSeenUtc = last.LastSeenUtc;
                    last = result; Render();
                })); }
                catch (InvalidOperationException) { }
            });
        }
        private static string LocalTime(string utc)
        {
            DateTime value;
            return DateTime.TryParse(utc, CultureInfo.InvariantCulture, DateTimeStyles.RoundtripKind, out value) ? value.ToLocalTime().ToString("HH:mm:ss") : "—";
        }
        private string Description()
        {
            if (last == null) return "IP: " + settings.Ip + "\r\n" + text[paused ? "Paused" : "Checking"];
            string[] names = text.ServiceNames(last);
            return "IP: " + settings.Ip + "\r\n" + text[paused ? "Paused" : last.Connected ? "Available" : "Unavailable"] +
                "\r\n" + text["Application"] + ": " + text.ApplicationName(last) + "\r\n" + text["Services"] + ": " + (names.Length == 0 ? text["NotFound"] : string.Join(", ", names)) +
                "\r\nPing: " + text[last.Ping ? "Yes" : "No"] + "\r\n" + text["Checked"] + ": " + LocalTime(last.CheckedUtc) + "\r\n" + text["LastSeen"] + ": " + LocalTime(last.LastSeenUtc) +
                "\r\n\r\n" + text["ProbeNote"];
        }
        private void Render()
        {
            detailsItem.Text = text["Details"]; changeIpItem.Text = text["ChangeIp"]; probeItem.Text = text["Probe"];
            pauseItem.Text = text["Pause"]; exitItem.Text = text["Exit"]; languageItem.Text = text["Language"];
            englishItem.Checked = settings.Language == "en"; russianItem.Checked = settings.Language == "ru";
            RefreshStartup();
            target.Text = "Switch · " + settings.Ip;
            connection.Text = text[paused ? "Pause" : last == null ? "Checking" : last.Connected ? "Connected" : "NoResponse"];
            application.Text = last == null ? text["Application"] + ": —" : text.ApplicationName(last);
            string[] names = last == null ? new string[0] : text.ServiceNames(last);
            services.Text = names.Length == 0 ? text["Services"] + ": —" : string.Join(" · ", names);
            updated.Text = text["Checked"] + ": " + (last == null ? "—" : LocalTime(last.CheckedUtc));
            tray.Icon = paused || last == null ? gray : last.Connected ? green : red;
            string tip = settings.Ip + "\n" + connection.Text + "\n" + (last == null ? "" : text.ApplicationName(last));
            tray.Text = tip.Length <= 63 ? tip : tip.Substring(0, 63);
            if (detailsText != null && !detailsText.IsDisposed) detailsText.Text = Description();
            if (last != null) { last.Paused = paused; try { JsonFiles.Write(statusPath, last); } catch (IOException) { } catch (UnauthorizedAccessException) { } }
        }
        private void ChangeLanguage(string language)
        {
            string previous = settings.Language;
            settings.Language = UiText.NormalizeLanguage(language);
            try { JsonFiles.Write(configPath, settings); }
            catch (Exception ex)
            {
                if (!(ex is IOException || ex is UnauthorizedAccessException)) throw;
                settings.Language = previous;
                MessageBox.Show(text["SaveError"], "Switch Monitor", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return;
            }
            text = new UiText(settings.Language); Render();
        }
        private void RefreshStartup()
        {
            startupItem.Text = text["StartWithWindows"];
            try
            {
                startupItem.Checked = startup.Enabled;
                startupItem.Enabled = true; startupItem.ToolTipText = "";
            }
            catch (Exception error)
            {
                if (!(error is IOException || error is UnauthorizedAccessException || error is SecurityException)) throw;
                startupItem.Checked = false; startupItem.Enabled = false; startupItem.ToolTipText = text["StartupSettingError"];
            }
        }
        private void ChangeStartup()
        {
            try { startup.SetEnabled(!startup.Enabled); }
            catch (Exception error)
            {
                if (!(error is IOException || error is UnauthorizedAccessException || error is SecurityException)) throw;
                MessageBox.Show(text["StartupSettingError"], "Switch Monitor", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
            RefreshStartup();
        }
        private void ShowDetails()
        {
            if (details != null && !details.IsDisposed) { details.Show(); details.Activate(); return; }
            details = new Form { Text = "Switch Monitor", ClientSize = new Size(540, 300), StartPosition = FormStartPosition.CenterScreen };
            detailsText = new TextBox { Multiline = true, ReadOnly = true, Dock = DockStyle.Fill, BorderStyle = BorderStyle.None, Font = new Font("Segoe UI", 10), Text = Description() };
            details.Controls.Add(detailsText); details.Show();
        }
        private void ChangeIp()
        {
            using (Form dialog = new Form { Text = text["ConsoleIp"], ClientSize = new Size(320, 106), FormBorderStyle = FormBorderStyle.FixedDialog, MaximizeBox = false, MinimizeBox = false, StartPosition = FormStartPosition.CenterScreen })
            {
                var field = new TextBox { Left = 16, Top = 16, Width = 288, Text = settings.Ip };
                var ok = new Button { Text = text["Save"], Left = 104, Top = 58, Width = 100 };
                var cancel = new Button { Text = text["Cancel"], Left = 212, Top = 58, Width = 92, DialogResult = DialogResult.Cancel };
                dialog.Controls.AddRange(new Control[] { field, ok, cancel }); dialog.AcceptButton = ok; dialog.CancelButton = cancel;
                ok.Click += (sender, args) => { string ip; if (!Settings.NormalizeIp(field.Text, out ip)) { MessageBox.Show(dialog, text["InvalidIp"]); return; } settings.Ip = ip; JsonFiles.Write(configPath, settings); last = null; dialog.DialogResult = DialogResult.OK; dialog.Close(); };
                if (dialog.ShowDialog() == DialogResult.OK) { Render(); BeginRound(); }
            }
        }
        protected override void ExitThreadCore()
        {
            stopped = true; timer.Stop(); timer.Dispose(); tray.Visible = false;
            var menu = tray.ContextMenuStrip; tray.Dispose(); menu.Dispose(); green.Dispose(); red.Dispose(); gray.Dispose();
            if (details != null) details.Dispose(); dispatcher.Dispose();
            base.ExitThreadCore();
        }
    }

    public static class Program
    {
        [STAThread]
        public static int Main(string[] args)
        {
            string root = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, ".."));
            string probeFile = null;
            for (int i = 0; i < args.Length; ++i)
            {
                if (args[i] == "--root" && i + 1 < args.Length) root = Path.GetFullPath(args[++i]);
                else if (args[i] == "--probe-once" && i + 1 < args.Length) probeFile = Path.GetFullPath(args[++i]);
                else return 2;
            }
            if (probeFile != null)
            {
                Settings s = Settings.Load(Path.Combine(root, "data", "config.json"));
                JsonFiles.Write(probeFile, NetworkProbe.Read(s.Ip, s.TimeoutMs)); return 0;
            }
            try
            {
                string id;
                using (SHA256 hash = SHA256.Create()) id = BitConverter.ToString(hash.ComputeHash(Encoding.UTF8.GetBytes(root.ToUpperInvariant()))).Replace("-", "").Substring(0, 24);
                bool created;
                using (Mutex mutex = new Mutex(true, "Local\\SwitchMonitor_" + id, out created))
                {
                    if (!created) return 0;
                    try { Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false); using (TrayMonitor monitor = new TrayMonitor(root)) Application.Run(monitor); }
                    finally { mutex.ReleaseMutex(); }
                }
                return 0;
            }
            catch (Exception) { var text = new UiText(Settings.Load(Path.Combine(root, "data", "config.json")).Language); MessageBox.Show(text["StartupError"], "Switch Monitor", MessageBoxButtons.OK, MessageBoxIcon.Error); return 1; }
        }
    }
}
