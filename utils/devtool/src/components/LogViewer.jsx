import React, { useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react';

const ROW_HEIGHT = 18;
const OVERSCAN_ROWS = 12;
const MIN_PAGE_LINES = 40;
const MAX_PAGE_LINES = 160;

function LogViewer() {
  const [lines, setLines] = useState([]);
  const [filter, setFilter] = useState('');
  const [autoRefresh, setAutoRefresh] = useState(false);
  const [hasMoreBefore, setHasMoreBefore] = useState(false);
  const [beforeOffset, setBeforeOffset] = useState(null);
  const [fileSize, setFileSize] = useState(0);
  const [error, setError] = useState(null);
  const [isLoading, setIsLoading] = useState(false);
  const [scrollTop, setScrollTop] = useState(0);
  const [viewportHeight, setViewportHeight] = useState(1);
  const contentRef = useRef(null);
  const isLoadingRef = useRef(false);
  const pendingScrollAdjustRef = useRef(0);
  const shouldScrollToBottomRef = useRef(false);

  const getPageLineCount = useCallback(() => {
    const height = contentRef.current?.clientHeight || 360;
    const screenLines = Math.ceil(height / ROW_HEIGHT);
    return Math.max(MIN_PAGE_LINES, Math.min(MAX_PAGE_LINES, screenLines + OVERSCAN_ROWS * 2));
  }, []);

  const loadLatest = useCallback(async () => {
    if (isLoadingRef.current) return;
    isLoadingRef.current = true;
    setIsLoading(true);
    setError(null);

    try {
      const page = await window.electronAPI.readLogLines({ maxLines: getPageLineCount() });
      setLines(page.lines || []);
      setBeforeOffset(page.startOffset);
      setHasMoreBefore(Boolean(page.hasMoreBefore));
      setFileSize(page.fileSize || 0);
      shouldScrollToBottomRef.current = true;
    } catch (err) {
      console.error('Failed to load log:', err);
      setError(err.message || 'Failed to load log file');
      setLines([]);
    } finally {
      isLoadingRef.current = false;
      setIsLoading(false);
    }
  }, [getPageLineCount]);

  const loadOlder = useCallback(async () => {
    if (isLoadingRef.current || !hasMoreBefore || beforeOffset === null) return;
    isLoadingRef.current = true;
    setIsLoading(true);
    setError(null);

    try {
      const page = await window.electronAPI.readLogLines({
        beforeOffset,
        maxLines: getPageLineCount()
      });
      const olderLines = page.lines || [];
      if (olderLines.length > 0) {
        pendingScrollAdjustRef.current += olderLines.length * ROW_HEIGHT;
        setLines(prev => [...olderLines, ...prev]);
      }
      setBeforeOffset(page.startOffset);
      setHasMoreBefore(Boolean(page.hasMoreBefore));
      setFileSize(page.fileSize || fileSize);
    } catch (err) {
      console.error('Failed to load older log lines:', err);
      setError(err.message || 'Failed to load older log lines');
    } finally {
      isLoadingRef.current = false;
      setIsLoading(false);
    }
  }, [beforeOffset, fileSize, getPageLineCount, hasMoreBefore]);

  useEffect(() => {
    loadLatest();
  }, [loadLatest]);

  useEffect(() => {
    if (!autoRefresh) return;

    const interval = setInterval(() => {
      loadLatest();
    }, 2000);

    return () => clearInterval(interval);
  }, [autoRefresh, loadLatest]);

  useEffect(() => {
    const element = contentRef.current;
    if (!element) return;

    const updateHeight = () => setViewportHeight(element.clientHeight || 1);
    updateHeight();

    const resizeObserver = new ResizeObserver(updateHeight);
    resizeObserver.observe(element);
    return () => resizeObserver.disconnect();
  }, []);

  useEffect(() => {
    const element = contentRef.current;
    if (!element) return;
    element.scrollTop = 0;
    setScrollTop(0);
  }, [filter]);

  useLayoutEffect(() => {
    const element = contentRef.current;
    if (!element) return;

    if (pendingScrollAdjustRef.current > 0) {
      element.scrollTop += pendingScrollAdjustRef.current;
      pendingScrollAdjustRef.current = 0;
      setScrollTop(element.scrollTop);
    } else if (shouldScrollToBottomRef.current) {
      element.scrollTop = element.scrollHeight;
      shouldScrollToBottomRef.current = false;
      setScrollTop(element.scrollTop);
    }
  }, [lines]);

  const displayedLines = useMemo(() => {
    if (!filter.trim()) return lines;

    const filterLower = filter.toLowerCase();
    return lines.filter(line => line.toLowerCase().includes(filterLower));
  }, [filter, lines]);

  function handleScroll(event) {
    const nextScrollTop = event.currentTarget.scrollTop;
    setScrollTop(nextScrollTop);

    if (!filter.trim() && nextScrollTop < ROW_HEIGHT * 8) {
      loadOlder();
    }
  }

  function clearLogView() {
    setLines([]);
    setBeforeOffset(null);
    setHasMoreBefore(false);
    setError(null);
  }

  const totalHeight = displayedLines.length * ROW_HEIGHT;
  const firstVisibleRow = Math.max(0, Math.floor(scrollTop / ROW_HEIGHT) - OVERSCAN_ROWS);
  const visibleRowCount = Math.ceil(viewportHeight / ROW_HEIGHT) + OVERSCAN_ROWS * 2;
  const visibleLines = displayedLines.slice(firstVisibleRow, firstVisibleRow + visibleRowCount);

  return (
    <div className="log-viewer">
      <div className="log-header">
        <input
          type="text"
          className="log-filter-input"
          placeholder="Filter loaded lines..."
          value={filter}
          onChange={(e) => setFilter(e.target.value)}
        />
        <div className="log-controls">
          <label className="auto-refresh-label">
            <input
              type="checkbox"
              checked={autoRefresh}
              onChange={(e) => setAutoRefresh(e.target.checked)}
            />
            Auto-refresh
          </label>
          <button className="btn btn-small" onClick={loadOlder} disabled={isLoading || !hasMoreBefore || Boolean(filter.trim())}>
            Older
          </button>
          <button className="btn btn-small" onClick={loadLatest} disabled={isLoading}>
            Latest
          </button>
          <button className="btn btn-small" onClick={clearLogView}>
            Clear
          </button>
        </div>
      </div>
      <div className="log-content" ref={contentRef} onScroll={handleScroll}>
        {error ? (
          <div className="log-message">{error}</div>
        ) : displayedLines.length > 0 ? (
          <div className="log-virtual-spacer" style={{ height: totalHeight }}>
            {visibleLines.map((line, index) => {
              const rowIndex = firstVisibleRow + index;
              return (
                <div
                  key={`${rowIndex}-${line.slice(0, 32)}`}
                  className="log-line"
                  style={{ transform: `translateY(${rowIndex * ROW_HEIGHT}px)` }}
                >
                  {line}
                </div>
              );
            })}
          </div>
        ) : (
          <div className="log-message">{isLoading ? 'Loading log...' : 'No log entries loaded'}</div>
        )}
      </div>
      <div className="log-footer">
        <span className="log-stats">
          Showing {visibleLines.length} of {displayedLines.length} loaded lines
          {fileSize > 0 ? ` from ${(fileSize / (1024 * 1024)).toFixed(1)} MB` : ''}
          {hasMoreBefore && !filter.trim() ? ' - scroll up for older lines' : ''}
          {filter.trim() ? ' - filter applies to loaded lines' : ''}
        </span>
      </div>
    </div>
  );
}

export default LogViewer;
