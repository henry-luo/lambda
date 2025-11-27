#!/bin/bash

# Layout DevTool Setup Script
# Run from the layout-devtool directory

set -e

echo "🚀 Setting up Layout DevTool..."
echo ""

# Check if we're in the right directory
if [ ! -f "package.json" ]; then
    echo "❌ Error: package.json not found"
    echo "   Please run this script from the layout-devtool directory"
    exit 1
fi

# Check Node.js version
NODE_VERSION=$(node --version | cut -d'v' -f2 | cut -d'.' -f1)
if [ "$NODE_VERSION" -lt 18 ]; then
    echo "❌ Error: Node.js 18+ required (found v$NODE_VERSION)"
    exit 1
fi

echo "✅ Node.js version: $(node --version)"
echo ""

# Install dependencies
echo "📦 Installing dependencies..."
npm install

echo ""
echo "✅ Setup complete!"
echo ""
echo "To run the app in development mode:"
echo "  npm run electron:dev"
echo ""
echo "To build for production:"
echo "  npm run build"
echo "  npm run package"
echo ""
