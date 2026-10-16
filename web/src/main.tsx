import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { BrowserRouter, NavLink, Route, Routes, useLocation } from 'react-router-dom';
import '@fontsource/ibm-plex-sans/400.css';
import '@fontsource/ibm-plex-sans/600.css';
import '@fontsource/ibm-plex-sans/700.css';
import '@fontsource/ibm-plex-mono/500.css';
import './styles/index.css';
import { ReplayBanner } from './components/bits';
import { REPLAY } from './lib/config';
import { Dashboard } from './routes/Dashboard';
import { Fleet } from './routes/Fleet';
import { Harness } from './routes/Harness';
import { Hmi } from './routes/Hmi';
import { Line } from './routes/Line';

function Shell() {
  const loc = useLocation();
  if (loc.pathname.startsWith('/hmi')) return <Routes><Route path="/hmi/:station" element={<Hmi />} /><Route path="/hmi" element={<Hmi />} /></Routes>;
  const link = ({ isActive }: { isActive: boolean }) => `rounded px-2.5 py-1 ${isActive ? 'bg-surface-2 text-ink' : 'text-muted hover:text-ink'}`;
  return (
    <div className="flex h-full flex-col">
      <ReplayBanner show={REPLAY} />
      <header className="flex items-center gap-4 border-b border-line px-4 py-2">
        <span className="font-semibold tracking-wide">Torque<span className="text-ok">Sense</span></span>
        <nav className="flex gap-1 text-sm">
          <NavLink to="/" end className={link}>Dashboard</NavLink>
          <NavLink to="/hmi/W-14" className={link}>HMI</NavLink>
          <NavLink to="/harness" className={link}>Harness</NavLink>
          <NavLink to="/line" className={link}>Line</NavLink>
          <NavLink to="/fleet" className={link}>Fleet</NavLink>
        </nav>
        <span className="ml-auto text-[12px] text-muted">CAD-compiled assembly verification · any assembly, as data</span>
      </header>
      <main className="min-h-0 flex-1 overflow-auto">
        <Routes>
          <Route path="/" element={<Dashboard />} />
          <Route path="/station/:station" element={<Dashboard />} />
          <Route path="/harness" element={<Harness />} />
          <Route path="/harness/:station" element={<Harness />} />
          <Route path="/line" element={<Line />} />
          <Route path="/fleet" element={<Fleet />} />
        </Routes>
      </main>
    </div>
  );
}

createRoot(document.getElementById('root')!).render(
  <StrictMode>
    <BrowserRouter>
      <Shell />
    </BrowserRouter>
  </StrictMode>,
);
