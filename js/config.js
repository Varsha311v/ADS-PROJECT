/* Where is the C++ backend?
 *  ""                              -> same origin (the C++ server also serves this site). Default.
 *  "https://your-api.example.com"  -> backend hosted elsewhere (e.g. site on Netlify, API on Render).
 *                                     Start the server with CORS_ORIGIN set to this site's origin.
 * If no backend answers, the app automatically runs fully in the browser instead. */
window.FRIEND_FINDER_API = "";
