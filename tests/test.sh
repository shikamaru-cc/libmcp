#!/bin/bash
# Simple test script for libmcp

set -e

echo "Building library and examples..."
make clean
make
make examples

echo -e "\n=== Testing Echo Server ==="
echo "Test 1: Initialize handshake"
RESULT=$(echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}' | ./examples/echo_server 2>/dev/null)
echo "Result: $RESULT"

# Check if result contains expected fields
if echo "$RESULT" | grep -q "protocolVersion" && echo "$RESULT" | grep -q "serverInfo"; then
    echo "✓ Initialize test passed"
else
    echo "✗ Initialize test failed"
    exit 1
fi

echo -e "\nTest 2: Echo request"
RESULT=$(echo -e '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}\n{"jsonrpc":"2.0","id":2,"method":"test","params":{"data":"hello"}}' | ./examples/echo_server 2>/dev/null | tail -1)
echo "Result: $RESULT"

if echo "$RESULT" | grep -q "echo"; then
    echo "✓ Echo test passed"
else
    echo "✗ Echo test failed"
    exit 1
fi

echo -e "\n=== Testing Tool Server ==="
echo "Test 3: Tools list"
RESULT=$(echo -e '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}\n{"jsonrpc":"2.0","id":2,"method":"tools/list"}' | ./examples/tool_server 2>/dev/null | tail -1)
echo "Result: $RESULT"

if echo "$RESULT" | grep -q "add" && echo "$RESULT" | grep -q "multiply"; then
    echo "✓ Tools list test passed"
else
    echo "✗ Tools list test failed"
    exit 1
fi

echo -e "\nTest 4: Tool execution (add)"
RESULT=$(echo -e '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}\n{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"add","arguments":{"a":5,"b":3}}}' | ./examples/tool_server 2>/dev/null | tail -1)
echo "Result: $RESULT"

if echo "$RESULT" | grep -q "Result: 8"; then
    echo "✓ Add tool test passed"
else
    echo "✗ Add tool test failed"
    exit 1
fi

echo -e "\nTest 5: Tool execution (multiply)"
RESULT=$(echo -e '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}\n{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"multiply","arguments":{"a":7,"b":6}}}' | ./examples/tool_server 2>/dev/null | tail -1)
echo "Result: $RESULT"

if echo "$RESULT" | grep -q "Result: 42"; then
    echo "✓ Multiply tool test passed"
else
    echo "✗ Multiply tool test failed"
    exit 1
fi

echo -e "\n=== All tests passed! ==="
