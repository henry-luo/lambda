import React, { useState, useEffect, forwardRef, useImperativeHandle } from 'react';

const PixelDiffViewer = forwardRef(({ renderTest }, ref) => {
  const [diffImage, setDiffImage] = useState(null);
  const [diffPercent, setDiffPercent] = useState(null);
  const [mismatchedPixels, setMismatchedPixels] = useState(null);
  const [totalPixels, setTotalPixels] = useState(null);

  useImperativeHandle(ref, () => ({
    updateDiff: (result) => {
      setDiffImage(result.diff);
      setDiffPercent(result.mismatchPercent);
      setMismatchedPixels(result.mismatchedPixels);
      setTotalPixels(result.totalPixels);
    }
  }));

  useEffect(() => {
    if (renderTest && renderTest.testType === 'render') {
      loadDiff(renderTest.testFile, renderTest.renderDir);
    } else {
      setDiffImage(null);
      setDiffPercent(null);
      setMismatchedPixels(null);
      setTotalPixels(null);
    }
  }, [renderTest]);

  async function loadDiff(testName, renderDir) {
    try {
      const imgs = await window.electronAPI.getRenderTestImages(testName, renderDir);
      setDiffImage(imgs.diff);
      // No stored diff percent — will be set after test run
    } catch (error) {
      console.error('Failed to load diff image:', error);
    }
  }

  return (
    <div className="pixel-diff-viewer">
      <div className="pixel-diff-stats">
        {diffPercent !== null && (
          <>
            <span className={`diff-badge ${diffPercent <= 1.0 ? 'pass' : diffPercent <= 5.0 ? 'warn' : 'fail'}`}>
              {diffPercent.toFixed(2)}%
            </span>
            <span className="diff-detail">
              {mismatchedPixels !== null && `${mismatchedPixels} / ${totalPixels} pixels`}
            </span>
          </>
        )}
        {diffPercent === null && diffImage && (
          <span className="diff-detail">Run test to compute diff %</span>
        )}
      </div>
      <div className="pixel-diff-content">
        {diffImage ? (
          <img src={diffImage} alt="Pixel Diff" className="render-image" />
        ) : (
          <div className="empty-state">
            <p>{renderTest ? 'No diff available — run test' : 'Select a render test'}</p>
          </div>
        )}
      </div>
    </div>
  );
});

PixelDiffViewer.displayName = 'PixelDiffViewer';
export default PixelDiffViewer;
