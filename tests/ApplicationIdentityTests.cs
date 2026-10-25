using System;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace SwitchMonitor
{
    public static class ApplicationIdentityTests
    {
        private static readonly string Token = new string('a', 64);
        private static void Check(bool condition, string message) { if (!condition) throw new Exception(message); }

        public static void Run()
        {
            string tokenFile = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "identity-token-" + Guid.NewGuid().ToString("N") + ".txt");
            File.WriteAllText(tokenFile, Token);
            try
            {
                foreach (int protocol in new[] { 1, 2 })
                {
                    WithPeer("OK AUTH\n", "{\"protocol\":" + protocol + ",\"app\":\"Пример игры\",\"version\":\"1.2.3\"}\n", true, port =>
                    {
                        var identity = ApplicationIdentityClient.Read("127.0.0.1", port, 1000, tokenFile);
                        Check(identity != null && identity.Name == "Пример игры" && identity.Version == "1.2.3", "Reads the actual Unicode name and version");
                        var snapshot = Snapshot.From("127.0.0.1", false, false, false, false, identity);
                        Check(snapshot.Connected && snapshot.Application == "Пример игры", "Authenticated application service alone proves connectivity");
                        Check(new UiText("ru").ApplicationName(snapshot) == "Пример игры" && new UiText("en").ApplicationName(snapshot) == "Пример игры", "Reported names are preserved in both languages");
                        Check(Snapshot.From("127.0.0.1", false, true, false, false).Application == "Application unknown", "A later failed lookup does not retain a stale name");
                    });
                }
                foreach (string invalid in new[] { "{}\n", "{bad}\n", "{\"protocol\":3,\"app\":\"Game\"}\n", "{\"protocol\":2,\"app\":\"\"}\n", "{\"protocol\":2,\"app\":\"Fake\\nstatus\"}\n", "{\"protocol\":2,\"app\":\"" + Token + "\"}\n", new string('x', 2048) + "\n" })
                    WithPeer("OK AUTH\n", invalid, true, port => Check(ApplicationIdentityClient.Read("127.0.0.1", port, 1000, tokenFile) == null, "Malformed, unsupported, overlong or token-bearing names are rejected"));
                WithPeer("OK AUTH\n", "{\"protocol\":2,\"app\":\"Example Game\",\"version\":\"\"}\n", true, port =>
                {
                    var identity = ApplicationIdentityClient.Read("127.0.0.1", port, 1000, tokenFile);
                    Check(identity != null && identity.Name == "Example Game" && identity.Version == null, "An empty optional version does not hide a valid application name");
                });
                WithPeer("ERR auth\n", null, false, port => Check(ApplicationIdentityClient.Read("127.0.0.1", port, 1000, tokenFile) == null, "Authentication failure sends no STATUS"));

                var listener = new TcpListener(IPAddress.Loopback, 0); listener.Start();
                int localPort = ((IPEndPoint)listener.LocalEndpoint).Port;
                try
                {
                    Check(ApplicationIdentityClient.Read("127.0.0.1", localPort, 150, null) == null && !listener.Pending(), "Unconfigured lookup never connects");
                    File.WriteAllText(tokenFile, "invalid");
                    Check(ApplicationIdentityClient.Read("127.0.0.1", localPort, 150, tokenFile) == null && !listener.Pending(), "Invalid token never connects");
                    File.WriteAllText(tokenFile, Token);
                    Task peer = Task.Run(() =>
                    {
                        using (TcpClient client = listener.AcceptTcpClient())
                        {
                            client.ReceiveTimeout = 1000;
                            using (var reader = new StreamReader(client.GetStream(), Encoding.ASCII))
                            {
                                Check(reader.ReadLine() == "AUTH " + Token, "Silent peer receives authentication only");
                                Thread.Sleep(350);
                                Check(reader.Read() == -1, "Timeout closes the socket without any control request");
                            }
                        }
                    });
                    Stopwatch clock = Stopwatch.StartNew();
                    Check(ApplicationIdentityClient.Read("127.0.0.1", localPort, 150, tokenFile) == null && clock.ElapsedMilliseconds < 1000, "A silent peer has a bounded deadline");
                    Check(peer.Wait(3000), "Silent peer finishes");
                }
                finally { listener.Stop(); }
            }
            finally { File.Delete(tokenFile); }
        }

        private static void WithPeer(string authReply, string reply, bool expectStatus, Action<int> query)
        {
            var listener = new TcpListener(IPAddress.Loopback, 0); listener.Start();
            int port = ((IPEndPoint)listener.LocalEndpoint).Port;
            Task peer = Task.Run(() =>
            {
                using (TcpClient client = listener.AcceptTcpClient())
                {
                    client.ReceiveTimeout = 2000;
                    NetworkStream stream = client.GetStream();
                    using (var reader = new StreamReader(stream, Encoding.ASCII, false, 1024, true))
                    {
                        Check(reader.ReadLine() == "AUTH " + Token, "Only AUTH is sent first");
                        byte[] auth = Encoding.ASCII.GetBytes(authReply); stream.Write(auth, 0, auth.Length);
                        if (expectStatus)
                        {
                            Check(reader.ReadLine() == "STATUS", "Only the read-only STATUS command follows authentication");
                            byte[] data = Encoding.UTF8.GetBytes(reply); stream.Write(data, 0, data.Length);
                        }
                        try { Check(reader.Read() == -1, "No input, launch, upload or debugging commands are sent"); }
                        catch (IOException error)
                        {
                            // Closing an overlong, unread reply can reset TCP on Windows.
                            var socketError = error.InnerException as SocketException;
                            if (reply == null || Encoding.UTF8.GetByteCount(reply) < 1024 || socketError == null || socketError.SocketErrorCode != SocketError.ConnectionReset) throw;
                        }
                    }
                }
            });
            try { query(port); Check(peer.Wait(4000), "Identity peer finishes"); }
            finally { listener.Stop(); }
        }
    }
}
