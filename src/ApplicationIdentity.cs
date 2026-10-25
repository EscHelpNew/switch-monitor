using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Net.Sockets;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Text;
using System.Threading;

namespace SwitchMonitor
{
    [DataContract]
    public sealed class ApplicationIdentity
    {
        [DataMember(Name = "protocol")] public int Protocol;
        [DataMember(Name = "app")] public string Name;
        [DataMember(Name = "version")] public string Version;
    }

    public static class ApplicationIdentityClient
    {
        public static string ResolveTokenFile(string root, string configuredFile)
        {
            if (string.IsNullOrWhiteSpace(configuredFile)) return null;
            try { return Path.GetFullPath(Path.IsPathRooted(configuredFile) ? configuredFile : Path.Combine(root, configuredFile)); }
            catch (Exception) { return null; }
        }

        private static string ReadToken(string path)
        {
            if (string.IsNullOrEmpty(path)) return null;
            using (FileStream file = File.OpenRead(path))
            {
                byte[] bytes = new byte[257]; int total = 0, count;
                while (total < bytes.Length && (count = file.Read(bytes, total, bytes.Length - total)) > 0) total += count;
                if (total > 256) return null;
                string token = new UTF8Encoding(false, true).GetString(bytes, 0, total).TrimStart('\uFEFF').Trim();
                if (token.Length != 64) return null;
                foreach (char c in token) if (!Uri.IsHexDigit(c)) return null;
                return token;
            }
        }

        public static bool ValidTokenFile(string path)
        {
            try { return ReadToken(path) != null; }
            catch (Exception) { return false; }
        }

        private static int Remaining(Stopwatch clock, int timeout)
        {
            long remaining = timeout - clock.ElapsedMilliseconds;
            if (remaining <= 0) throw new TimeoutException();
            return (int)remaining;
        }

        private static void Send(TcpClient client, NetworkStream stream, string command, Stopwatch clock, int timeout)
        {
            client.SendTimeout = Remaining(clock, timeout);
            byte[] bytes = Encoding.ASCII.GetBytes(command + "\n");
            stream.Write(bytes, 0, bytes.Length);
        }

        private static string Line(TcpClient client, NetworkStream stream, Stopwatch clock, int timeout)
        {
            var bytes = new List<byte>();
            while (bytes.Count < 1024)
            {
                client.ReceiveTimeout = Remaining(clock, timeout);
                int value = stream.ReadByte();
                if (value < 0) throw new EndOfStreamException();
                if (value == '\n') return new UTF8Encoding(false, true).GetString(bytes.ToArray()).TrimEnd('\r');
                bytes.Add((byte)value);
            }
            throw new InvalidDataException();
        }

        private static bool SafeText(string value, int maximum)
        {
            if (string.IsNullOrWhiteSpace(value) || value.Length > maximum) return false;
            foreach (char c in value) if (char.IsControl(c)) return false;
            return true;
        }

        public static ApplicationIdentity Read(string ip, int port, int timeout, string tokenFile)
        {
            try
            {
                string token = ReadToken(tokenFile);
                if (token == null) return null;
                var clock = Stopwatch.StartNew();
                using (var client = new TcpClient(AddressFamily.InterNetwork))
                {
                    IAsyncResult connect = client.BeginConnect(ip, port, null, null);
                    using (WaitHandle wait = connect.AsyncWaitHandle)
                    {
                        if (!wait.WaitOne(Remaining(clock, timeout))) return null;
                        client.EndConnect(connect);
                    }
                    using (NetworkStream stream = client.GetStream())
                    {
                        Send(client, stream, "AUTH " + token, clock, timeout);
                        if (Line(client, stream, clock, timeout) != "OK AUTH") return null;
                        Send(client, stream, "STATUS", clock, timeout);
                        byte[] json = Encoding.UTF8.GetBytes(Line(client, stream, clock, timeout));
                        using (var input = new MemoryStream(json))
                        {
                            var identity = (ApplicationIdentity)new DataContractJsonSerializer(typeof(ApplicationIdentity)).ReadObject(input);
                            if (identity == null || (identity.Protocol != 1 && identity.Protocol != 2) || !SafeText(identity.Name, 128)) return null;
                            if (!string.IsNullOrEmpty(identity.Version) && !SafeText(identity.Version, 64)) return null;
                            if (identity.Name.IndexOf(token, StringComparison.OrdinalIgnoreCase) >= 0 || (identity.Version != null && identity.Version.IndexOf(token, StringComparison.OrdinalIgnoreCase) >= 0)) return null;
                            identity.Name = identity.Name.Trim();
                            if (identity.Version == "") identity.Version = null;
                            return identity;
                        }
                    }
                }
            }
            catch (Exception) { return null; }
        }
    }
}
