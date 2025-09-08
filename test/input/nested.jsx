<div className="container">
  <header>
    <nav>
      <Link to="/home">Home</Link>
      <Link to="/about">About</Link>
    </nav>
  </header>
  <main>
    <section>
      <h1>{title}</h1>
      <p>Welcome to {siteName}!</p>
      <button onClick={() => setCount(count + 1)}>
        Count: {count}
      </button>
    </section>
  </main>
</div>
