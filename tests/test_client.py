#!/usr/bin/env python3
import subprocess
import sys
import struct

def send_message(proc, message):
    message += "\r\n"
    content = message.encode('utf-8')
    proc.stdin.write(content)
    proc.stdin.flush()

def read_message(proc):
    return proc.stdout.readline()

proc = subprocess.Popen(["./build/hello"],
                       stdin=subprocess.PIPE,
                       stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE)

try:
    send_message(proc, '{"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"opencode","version":"1.1.37"}},"jsonrpc":"2.0","id":0}')
    response = read_message(proc)
    print(f"Initialize: {response}")

    send_message(proc, '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}')
    response = read_message(proc)
    print(f"Tools list: {response}")

    send_message(proc, '{"jsonrpc":"2.0","id":3,"method":"prompts/list","params":{}}')
    response = read_message(proc)
    print(f"Prompts list: {response}")

    send_message(proc, '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"add","arguments":{"a":5,"b":3}}}')
    response = read_message(proc)
    print(f"Add 5+3: {response}")

    send_message(proc, '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"multiply","arguments":{"a":4,"b":6}}}')
    response = read_message(proc)
    print(f"Multiply 4*6: {response}")

    send_message(proc, '{"jsonrpc":"2.0","id":6,"method":"prompts/get","params":{"name":"sample","arguments":{}}}')
    response = read_message(proc)
    print(f"Get prompt: {response}")

finally:
    proc.terminate()
    proc.wait()
    stderr = proc.stderr.read().decode('utf-8')
    if stderr:
        print(f"\nServer output: {stderr}")
