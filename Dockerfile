# Stage 1: Build the C++ chess engine binary
FROM python:3.12-slim-bookworm AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY include/ ./include/
COPY src/ ./src/

RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF \
    && cmake --build build --target chess_engine

# Stage 2: Runtime environment
FROM python:3.12-slim-bookworm AS runner

WORKDIR /app

# Copy compiled engine binary from builder
COPY --from=builder /src/build/chess_engine /app/build/chess_engine

# Copy API adapter and requirements
COPY api/ /app/api/
COPY networks/ /app/networks/

# Install Python dependencies
RUN pip install --no-cache-dir -r /app/api/requirements.txt

# Set environment variables
ENV PYTHONUNBUFFERED=1
ENV CHESS_ENGINE_PATH=/app/build/chess_engine

# Render sets PORT dynamically (defaults to 8000 for local Docker testing)
ENV PORT=8000
EXPOSE ${PORT}

# Run Uvicorn via exec so SIGTERM is handled gracefully on shutdown
CMD ["sh", "-c", "exec uvicorn api.app:app --host 0.0.0.0 --port ${PORT:-8000}"]

