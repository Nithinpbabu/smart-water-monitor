#!/bin/bash

cd /home/nas/Documents/Rpi-Dashboard || exit 1

echo "🔨 Building dashboard..."
docker compose build --no-cache || exit 1

echo "🚀 Starting dashboard..."
docker compose up -d || exit 1

echo "✅ Dashboard started."
echo "📋 Showing logs (Ctrl+C to exit logs)..."
docker compose logs -f dashboard