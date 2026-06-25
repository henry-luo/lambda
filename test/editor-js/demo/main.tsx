import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { VERSION } from '../src/editor'

function App() {
  return (
    <main style={{ fontFamily: 'system-ui', padding: 24 }}>
      <h1>Lambda Editor — JS reference</h1>
      <p>Bootstrap skeleton. Library version: <code>{VERSION}</code></p>
      <p>The editor view layer is not built yet; see task #7 in the project task list.</p>
    </main>
  )
}

const root = document.getElementById('root')
if (root) {
  createRoot(root).render(
    <StrictMode>
      <App />
    </StrictMode>
  )
}
