/* cheatah docs — search, theme toggle, active-nav + TOC scrollspy. Vanilla JS. */
(function () {
  "use strict";

  // ---- theme ----
  var root = document.documentElement;
  var saved = localStorage.getItem("cheatah-theme");
  if (saved) root.setAttribute("data-theme", saved);
  var tbtn = document.getElementById("theme");
  if (tbtn) tbtn.addEventListener("click", function () {
    var next = root.getAttribute("data-theme") === "light" ? "dark" : "light";
    root.setAttribute("data-theme", next);
    localStorage.setItem("cheatah-theme", next);
  });

  // ---- highlight current page in sidebar ----
  var here = location.pathname.split("/").pop() || "index.html";
  document.querySelectorAll('.sidebar li a').forEach(function (a) {
    if (a.getAttribute("href") === here) a.classList.add("active");
  });

  // ---- search ----
  var q = document.getElementById("q");
  var box = document.getElementById("results");
  var data = window.SEARCH || [];
  var active = -1, shown = [];

  function render(list) {
    if (!list.length) { box.innerHTML = '<div class="empty">No matches</div>'; box.hidden = false; return; }
    box.innerHTML = list.map(function (e, i) {
      var ctx = e.c ? '<span class="r-ctx">' + e.c + '</span>' : "";
      return '<a href="' + e.u + '" data-i="' + i + '"><span class="r-kind">' + e.k + '</span>' +
             '<span class="r-name">' + e.n + '</span>' + ctx + '</a>';
    }).join("");
    box.hidden = false;
  }

  function search(term) {
    term = term.trim().toLowerCase();
    if (!term) { box.hidden = true; shown = []; active = -1; return; }
    var starts = [], contains = [];
    for (var i = 0; i < data.length; i++) {
      var n = data[i].n.toLowerCase();
      if (n === term || n.indexOf(term) === 0) starts.push(data[i]);
      else if (n.indexOf(term) !== -1) contains.push(data[i]);
    }
    shown = starts.concat(contains).slice(0, 40);
    active = -1;
    render(shown);
  }

  function move(d) {
    var links = box.querySelectorAll("a");
    if (!links.length) return;
    if (active >= 0) links[active].classList.remove("active");
    active = (active + d + links.length) % links.length;
    links[active].classList.add("active");
    links[active].scrollIntoView({ block: "nearest" });
  }

  if (q) {
    q.addEventListener("input", function () { search(q.value); });
    q.addEventListener("keydown", function (e) {
      if (e.key === "ArrowDown") { e.preventDefault(); move(1); }
      else if (e.key === "ArrowUp") { e.preventDefault(); move(-1); }
      else if (e.key === "Enter") {
        var links = box.querySelectorAll("a");
        if (links.length) { e.preventDefault(); (links[active] || links[0]).click(); }
      } else if (e.key === "Escape") { box.hidden = true; q.blur(); }
    });
    document.addEventListener("click", function (e) {
      if (!e.target.closest(".search")) box.hidden = true;
    });
    document.addEventListener("keydown", function (e) {
      if (e.key === "/" && document.activeElement !== q) { e.preventDefault(); q.focus(); }
    });
  }

  // ---- TOC scrollspy ----
  var tocLinks = Array.prototype.slice.call(document.querySelectorAll(".toc a"));
  if (tocLinks.length && "IntersectionObserver" in window) {
    var map = {};
    tocLinks.forEach(function (a) { map[a.getAttribute("href").slice(1)] = a; });
    var io = new IntersectionObserver(function (entries) {
      entries.forEach(function (en) {
        if (en.isIntersecting) {
          tocLinks.forEach(function (a) { a.classList.remove("active"); });
          var link = map[en.target.id];
          if (link) link.classList.add("active");
        }
      });
    }, { rootMargin: "-72px 0px -70% 0px" });
    document.querySelectorAll("section.member[id]").forEach(function (s) { io.observe(s); });
  }
})();
