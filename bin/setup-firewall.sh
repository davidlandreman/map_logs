#!/bin/bash
# Setup macOS pf firewall rules for VibeCraft multiplayer
# Opens TCP 52080 and UDP 52099 for inbound connections

set -e

ANCHOR_NAME="vibecraft"
ANCHOR_FILE="/etc/pf.anchors/$ANCHOR_NAME"
PF_CONF="/etc/pf.conf"

echo "Setting up firewall rules for VibeCraft..."

# Create the anchor file
echo "Creating $ANCHOR_FILE..."
sudo tee "$ANCHOR_FILE" > /dev/null << 'EOF'
pass in proto tcp from any to any port 52080
pass in proto udp from any to any port 52099
EOF

# Check if anchor already exists in pf.conf
if ! grep -q "anchor \"$ANCHOR_NAME\"" "$PF_CONF"; then
    echo "Adding anchor to $PF_CONF..."
    sudo tee -a "$PF_CONF" > /dev/null << EOF
anchor "$ANCHOR_NAME"
load anchor "$ANCHOR_NAME" from "$ANCHOR_FILE"
EOF
else
    echo "Anchor already exists in $PF_CONF, skipping..."
fi

# Load and enable pf
echo "Loading pf rules..."
sudo pfctl -f "$PF_CONF"
sudo pfctl -e 2>/dev/null || true

# Verify
echo ""
echo "Verifying rules:"
sudo pfctl -a "$ANCHOR_NAME" -sr

echo ""
echo "Firewall setup complete. Ports open:"
echo "  - TCP 52080 (inbound)"
echo "  - UDP 52099 (inbound)"
