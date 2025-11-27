import React, { useState, useEffect, useRef, useImperativeHandle, forwardRef } from 'react';
import { Terminal } from 'xterm';
import { FitAddon } from 'xterm-addon-fit';
import 'xterm/css/xterm.css';

const TerminalPanel = forwardRef((props, ref) => {
  const terminalRef = useRef(null);
  const xtermRef = useRef(null);
  const fitAddonRef = useRef(null);
  const [isReady, setIsReady] = useState(false);
  const resizeObserverRef = useRef(null);

  useEffect(() => {
    // Wait for DOM to be ready
    const timer = setTimeout(() => {
      if (!terminalRef.current || xtermRef.current) return;

      try {
        const terminal = new Terminal({
          rows: 30,
          cols: 120,  // Fixed width, don't auto-fit
          scrollback: 2000,
          convertEol: true,  // Handle line endings properly
          theme: {
            background: '#1e1e1e',
            foreground: '#d4d4d4',
            cursor: '#d4d4d4',
            selection: 'rgba(255, 255, 255, 0.3)'
          },
          fontSize: 13,
          fontFamily: 'Monaco, Menlo, Consolas, "Courier New", monospace',
          cursorBlink: false,
          scrollback: 2000
        });

        const fitAddon = new FitAddon();
        terminal.loadAddon(fitAddon);
        terminal.open(terminalRef.current);

        xtermRef.current = terminal;
        fitAddonRef.current = fitAddon;

        // Wait for terminal to be fully rendered
        setTimeout(() => {
          try {
            if (terminalRef.current?.offsetWidth > 0) {
              fitAddon.fit();
            }
          } catch (e) {
            console.log('Initial fit skipped');
          }

          terminal.writeln('\x1b[1;36mLayout DevTool Terminal\x1b[0m');
          terminal.writeln('Ready to run tests.');
          terminal.writeln('');
          setIsReady(true);
        }, 100);

        // Handle window resize - but don't change cols
        const handleResize = () => {
          // Keep cols fixed to prevent text wrapping issues
          // Only adjust rows based on container height
          if (terminalRef.current && xtermRef.current) {
            const containerHeight = terminalRef.current.offsetHeight;
            const lineHeight = 17; // Approximate line height in pixels
            const newRows = Math.max(10, Math.floor(containerHeight / lineHeight));
            try {
              xtermRef.current.resize(120, newRows);
            } catch (e) {
              // Ignore resize errors
            }
          }
        };

        // Use ResizeObserver to detect container size changes
        resizeObserverRef.current = new ResizeObserver(() => {
          handleResize();
        });

        if (terminalRef.current) {
          resizeObserverRef.current.observe(terminalRef.current);
        }

        window.addEventListener('resize', handleResize);

        // Listen for terminal output from main process
        if (window.electronAPI) {
          window.electronAPI.onTerminalOutput((data) => {
            terminal.write(data);
          });
        }

        return () => {
          window.removeEventListener('resize', handleResize);
          if (resizeObserverRef.current) {
            resizeObserverRef.current.disconnect();
          }
          if (window.electronAPI) {
            window.electronAPI.removeTerminalOutputListener();
          }
          terminal.dispose();
        };
      } catch (error) {
        console.error('Failed to initialize terminal:', error);
      }
    }, 100);

    return () => clearTimeout(timer);
  }, []);

  useImperativeHandle(ref, () => ({
    write: (text) => {
      xtermRef.current?.write(text);
    },
    writeln: (text) => {
      xtermRef.current?.writeln(text);
    },
    clear: () => {
      xtermRef.current?.clear();
    }
  }));

  return (
    <div className="terminal-panel">
      <div className="terminal-header">
        <span className="terminal-title">Terminal</span>
        <button
          className="btn btn-small"
          onClick={() => xtermRef.current?.clear()}
          title="Clear terminal"
        >
          Clear
        </button>
      </div>
      <div ref={terminalRef} className="terminal-content" />
    </div>
  );
});

TerminalPanel.displayName = 'TerminalPanel';

export default TerminalPanel;
