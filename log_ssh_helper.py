"""
SSH 日志收集助手 - TCP 服务器
与 Qt 主程序通过 TCP 通信，处理 SSH 远程连接、文件列表、SFTP 下载、压缩打包
协议: [4字节大端序长度][JSON消息体]
"""

import socket
import struct
import json
import threading
import sys
import os
import io
import time
import zipfile
import tempfile
import re

if sys.platform == 'win32':
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace')

import paramiko

MSG_LOG = 0x4C
MSG_STATUS = 0x53


class SshHelperServer:
    """SSH 日志收集助手 TCP 服务器"""

    def __init__(self, host='127.0.0.1', port=0):
        self.host = host
        self.port = port
        self.conn = None
        self.server_socket = None
        self._send_lock = threading.Lock()
        # SSH 连接池: {host_key: paramiko.SSHClient}
        self._ssh_clients = {}
        self._sftp_clients = {}

    # ---- 启动服务器 ----
    def start(self):
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind((self.host, self.port))
        self.server_socket.listen(1)
        actual_port = self.server_socket.getsockname()[1]
        print(f"READY {actual_port}", flush=True)

        try:
            self.conn, addr = self.server_socket.accept()
            self.conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self._handle_client()
        except Exception as e:
            print(f"Server error: {e}", file=sys.stderr, flush=True)
        finally:
            self._cleanup()
            if self.conn:
                try:
                    self.conn.close()
                except:
                    pass
            self.server_socket.close()

    # ---- 网络收发 ----
    def _recv_exact(self, n):
        data = b''
        while len(data) < n:
            chunk = self.conn.recv(n - len(data))
            if not chunk:
                return None
            data += chunk
        return data

    def _recv_message(self):
        header = self._recv_exact(4)
        if not header:
            return None
        length = struct.unpack('>I', header)[0]
        if length > 10 * 1024 * 1024:
            return None
        return self._recv_exact(length)

    def _send_message(self, msg_type, data):
        payload = bytes([msg_type]) + json.dumps(data, ensure_ascii=False).encode('utf-8')
        header = struct.pack('>I', len(payload))
        with self._send_lock:
            try:
                self.conn.sendall(header + payload)
            except:
                pass

    def _send_status(self, data):
        self._send_message(MSG_STATUS, data)

    def _send_log(self, level, msg):
        self._send_message(MSG_LOG, {"level": level, "msg": msg})

    # ---- 命令处理 ----
    def _handle_client(self):
        while True:
            try:
                data = self._recv_message()
                if data is None:
                    break
                cmd = json.loads(data.decode('utf-8'))
                self._process_command(cmd)
            except (ConnectionResetError, ConnectionAbortedError, BrokenPipeError):
                break
            except json.JSONDecodeError as e:
                self._send_log("error", f"JSON解析错误: {e}")
            except Exception as e:
                self._send_log("error", f"命令错误: {e}")

    def _process_command(self, cmd):
        action = cmd.get("cmd", "")
        if action == "connect":
            self._cmd_connect(cmd)
        elif action == "disconnect":
            self._cmd_disconnect(cmd)
        elif action == "list_files":
            self._cmd_list_files(cmd)
        elif action == "get_robot_info":
            self._cmd_get_robot_info(cmd)
        elif action == "download":
            self._cmd_download(cmd)
        elif action == "exec_cmd":
            self._cmd_exec(cmd)
        elif action == "ping":
            self._send_status({"status": "pong"})

    # ---- SSH 连接 ----
    def _get_host_key(self, cmd):
        return f"{cmd.get('host', '')}:{cmd.get('port', 22)}"

    def _cmd_connect(self, cmd):
        host = cmd.get("host", "")
        port = cmd.get("port", 22)
        user = cmd.get("user", "root")
        password = cmd.get("password", "")
        host_key = self._get_host_key(cmd)

        try:
            self._send_log("info", f"正在连接 {user}@{host}:{port} ...")
            client = paramiko.SSHClient()
            client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
            client.connect(host, port=port, username=user, password=password, timeout=10)
            sftp = client.open_sftp()

            self._ssh_clients[host_key] = client
            self._sftp_clients[host_key] = sftp

            self._send_log("info", f"SSH 连接成功: {host}:{port}")
            self._send_status({"status": "connected", "host": host, "port": port})
        except Exception as e:
            self._send_log("error", f"SSH 连接失败: {e}")
            self._send_status({"status": "connect_failed", "host": host, "error": str(e)})

    def _cmd_disconnect(self, cmd):
        host_key = self._get_host_key(cmd)
        self._close_connection(host_key)
        self._send_status({"status": "disconnected", "host": cmd.get("host", "")})
        self._send_log("info", f"已断开: {cmd.get('host', '')}")

    def _close_connection(self, host_key):
        if host_key in self._sftp_clients:
            try:
                self._sftp_clients[host_key].close()
            except:
                pass
            del self._sftp_clients[host_key]
        if host_key in self._ssh_clients:
            try:
                self._ssh_clients[host_key].close()
            except:
                pass
            del self._ssh_clients[host_key]

    # ---- 文件列表 ----
    def _cmd_list_files(self, cmd):
        host_key = self._get_host_key(cmd)
        remote_path = cmd.get("remote_path", "/var/opt/robot/log/")
        sftp = self._sftp_clients.get(host_key)
        if not sftp:
            self._send_log("error", "未连接，无法列出文件")
            return

        try:
            files = []
            for attr in sftp.listdir_attr(remote_path):
                files.append({
                    "name": attr.filename,
                    "size": attr.st_size or 0,
                    "mtime": attr.st_mtime or 0,
                    "is_dir": (attr.st_mode & 0o40000) != 0 if attr.st_mode else False,
                })
            # 按修改时间降序排列
            files.sort(key=lambda x: x["mtime"], reverse=True)
            self._send_status({
                "status": "file_list",
                "host": cmd.get("host", ""),
                "path": remote_path,
                "files": files,
            })
        except Exception as e:
            self._send_log("error", f"列出文件失败 [{remote_path}]: {e}")

    # ---- 获取四足机器狗信息 ----
    def _cmd_get_robot_info(self, cmd):
        """从 103 获取 SN/VERSION/DEVICE_TYPE"""
        host_key = self._get_host_key(cmd)
        ssh = self._ssh_clients.get(host_key)
        if not ssh:
            self._send_log("error", "未连接，无法获取四足机器狗信息")
            return

        info = {"sn": "unknown", "version": "unknown", "device_type": "unknown"}
        try:
            # 读取 SN (MFI.toml)
            sftp = self._sftp_clients[host_key]
            try:
                with sftp.open("/var/opt/robot/conf/MFI.toml", "r") as f:
                    content = f.read().decode("utf-8", errors="replace")
                    match = re.search(r'sn\s*=\s*["\']?([^"\'\s\n]+)', content)
                    if match:
                        info["sn"] = match.group(1)
            except Exception as e:
                self._send_log("warn", f"读取 MFI.toml 失败: {e}")

            # 读取 VERSION / DEVICE_TYPE (release_note.json)
            try:
                with sftp.open("/var/opt/robot/release_note.json", "r") as f:
                    content = f.read().decode("utf-8", errors="replace")
                    data = json.loads(content)
                    info["version"] = data.get("VERSION", "unknown")
                    info["device_type"] = data.get("DEVICE_TYPE", "unknown")
            except Exception as e:
                self._send_log("warn", f"读取 release_note.json 失败: {e}")

            self._send_status({"status": "robot_info", **info})
            self._send_log("info",
                           f"四足机器狗信息: SN={info['sn']}, 版本={info['version']}, 构型={info['device_type']}")
        except Exception as e:
            self._send_log("error", f"获取四足机器狗信息失败: {e}")

    # ---- 下载并压缩 ----
    def _cmd_download(self, cmd):
        host_key = self._get_host_key(cmd)
        sftp = self._sftp_clients.get(host_key)
        if not sftp:
            self._send_log("error", "未连接，无法下载")
            return

        files = cmd.get("files", [])          # [{"remote_path":..., "name":...}, ...]
        local_dir = cmd.get("local_dir", "")
        zip_name = cmd.get("zip_name", "logs.zip")

        if not files:
            self._send_log("warn", "没有选择任何文件")
            return

        os.makedirs(local_dir, exist_ok=True)
        zip_path = os.path.join(local_dir, zip_name)
        total = len(files)

        try:
            self._send_log("info", f"开始下载 {total} 个文件...")
            with zipfile.ZipFile(zip_path, 'w', zipfile.ZIP_DEFLATED) as zf:
                for idx, file_info in enumerate(files):
                    remote_file = file_info.get("remote_path", "")
                    name = file_info.get("name", os.path.basename(remote_file))

                    self._send_status({
                        "status": "download_progress",
                        "current": idx + 1,
                        "total": total,
                        "file": name,
                        "percent": int((idx / total) * 100),
                    })

                    # 下载到临时文件再写入 zip
                    tmp_fd, tmp_path = tempfile.mkstemp()
                    try:
                        os.close(tmp_fd)
                        sftp.get(remote_file, tmp_path)
                        zf.write(tmp_path, arcname=name)
                    finally:
                        if os.path.exists(tmp_path):
                            os.unlink(tmp_path)

            self._send_status({
                "status": "download_complete",
                "zip_path": zip_path,
                "total_files": total,
            })
            self._send_log("info", f"下载完成: {zip_path} ({total} 个文件)")
        except Exception as e:
            self._send_log("error", f"下载失败: {e}")
            self._send_status({"status": "download_error", "error": str(e)})

    # ---- 远程命令 ----
    def _cmd_exec(self, cmd):
        host_key = self._get_host_key(cmd)
        ssh = self._ssh_clients.get(host_key)
        if not ssh:
            self._send_log("error", "未连接，无法执行命令")
            return

        command = cmd.get("command", "")
        try:
            stdin, stdout, stderr = ssh.exec_command(command, timeout=30)
            out = stdout.read().decode("utf-8", errors="replace").strip()
            err = stderr.read().decode("utf-8", errors="replace").strip()
            self._send_status({
                "status": "exec_result",
                "stdout": out,
                "stderr": err,
                "command": command,
            })
        except Exception as e:
            self._send_log("error", f"执行命令失败 [{command}]: {e}")

    # ---- 清理 ----
    def _cleanup(self):
        for key in list(self._ssh_clients.keys()):
            self._close_connection(key)


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 0
    server = SshHelperServer(port=port)
    server.start()


if __name__ == '__main__':
    main()
