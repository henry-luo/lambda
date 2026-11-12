# Supply a separately qualified Linux runtime image with Python and /app/lambda.exe.
ARG LAMBDA_RUNTIME_IMAGE
FROM ${LAMBDA_RUNTIME_IMAGE}
WORKDIR /app
COPY lmd /app/lmd
COPY deploy/sync_cloud/http_host.py /app/http_host.py
ENV LAMBDA_ROOT=/app PYTHONDONTWRITEBYTECODE=1
RUN mkdir -p /app/temp
CMD ["python3", "/app/http_host.py", "--bind", "0.0.0.0"]
