*HTTP server in c language*

Please use the files from db folder to create a sqlite3 database with name gttp.db.
For now this needs to be in ../db/gttp.db as this has been hard coded.

It assumes sqlite3-dev has been installed.

**Why this?**

1. I am just learning stuff and trying things out. I have very little formal training or knowledge in c as you might guess looking at the code ;-)
2. The key feature here is use of sqlite for storing connection related info (not all info but some) - not in memory structures.
3. Lots and lots needs to be done. All comments, criticism, improvements etc welcome
4. Right now it serves a json file of countries from the country table for all requests. Doesn't parse the request URL etc.




