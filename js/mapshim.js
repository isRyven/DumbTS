/*! *****************************************************************************
Copyright (c) Microsoft Corporation. All rights reserved.
Licensed under the Apache License, Version 2.0 (the "License"); you may not use
this file except in compliance with the License. You may obtain a copy of the
License at http://www.apache.org/licenses/LICENSE-2.0

THIS CODE IS PROVIDED ON AN *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
MERCHANTABLITY OR NON-INFRINGEMENT.

See the Apache Version 2.0 License for specific language governing permissions
and limitations under the License.
***************************************************************************** */


function createDictionaryObject() {
    var map = Object.create(/*prototype*/ null); // tslint:disable-line:no-null-keyword
    // Using 'delete' on an object causes V8 to put the object in dictionary mode.
    // This disables creation of hidden classes, which are expensive when an object is
    // constantly changing shape.
    map.__ = undefined;
    delete map.__;
    return map;
}

// Keep the class inside a function so it doesn't get compiled if it's not used.
function shimMap() {
    var MapIterator = /** @class */ (function () {
        function MapIterator(currentEntry, selector) {
            this.currentEntry = currentEntry;
            this.selector = selector;
        }
        MapIterator.prototype.next = function () {
            // Navigate to the next entry.
            while (this.currentEntry) {
                var skipNext = !!this.currentEntry.skipNext;
                this.currentEntry = this.currentEntry.nextEntry;
                if (!skipNext) {
                    break;
                }
            }
            if (this.currentEntry) {
                return { value: this.selector(this.currentEntry.key, this.currentEntry.value), done: false };
            }
            else {
                return { value: undefined, done: true };
            }
        };
        return MapIterator;
    }());
    return /** @class */ (function () {
        function class_1() {
            this.data = createDictionaryObject();
            this.size = 0;
            // Create a first (stub) map entry that will not contain a key
            // and value but serves as starting point for iterators.
            this.firstEntry = {};
            // When the map is empty, the last entry is the same as the
            // first one.
            this.lastEntry = this.firstEntry;
        }
        class_1.prototype.get = function (key) {
            var entry = this.data[key];
            return entry && entry.value;
        };
        class_1.prototype.set = function (key, value) {
            if (!this.has(key)) {
                this.size++;
                // Create a new entry that will be appended at the
                // end of the linked list.
                var newEntry = {
                    key: key,
                    value: value
                };
                this.data[key] = newEntry;
                // Adjust the references.
                var previousLastEntry = this.lastEntry;
                previousLastEntry.nextEntry = newEntry;
                newEntry.previousEntry = previousLastEntry;
                this.lastEntry = newEntry;
            }
            else {
                this.data[key].value = value;
            }
            return this;
        };
        class_1.prototype.has = function (key) {
            // tslint:disable-next-line:no-in-operator
            return key in this.data;
        };
        class_1.prototype.delete = function (key) {
            if (this.has(key)) {
                this.size--;
                var entry = this.data[key];
                delete this.data[key];
                // Adjust the linked list references of the neighbor entries.
                var previousEntry = entry.previousEntry;
                previousEntry.nextEntry = entry.nextEntry;
                if (entry.nextEntry) {
                    entry.nextEntry.previousEntry = previousEntry;
                }
                // When the deleted entry was the last one, we need to
                // adjust the lastEntry reference.
                if (this.lastEntry === entry) {
                    this.lastEntry = previousEntry;
                }
                // Adjust the forward reference of the deleted entry
                // in case an iterator still references it. This allows us
                // to throw away the entry, but when an active iterator
                // (which points to the current entry) continues, it will
                // navigate to the entry that originally came before the
                // current one and skip it.
                entry.previousEntry = undefined;
                entry.nextEntry = previousEntry;
                entry.skipNext = true;
                return true;
            }
            return false;
        };
        class_1.prototype.clear = function () {
            this.data = createDictionaryObject();
            this.size = 0;
            // Reset the linked list. Note that we must adjust the forward
            // references of the deleted entries to ensure iterators stuck
            // in the middle of the list don't continue with deleted entries,
            // but can continue with new entries added after the clear()
            // operation.
            var firstEntry = this.firstEntry;
            var currentEntry = firstEntry.nextEntry;
            while (currentEntry) {
                var nextEntry = currentEntry.nextEntry;
                currentEntry.previousEntry = undefined;
                currentEntry.nextEntry = firstEntry;
                currentEntry.skipNext = true;
                currentEntry = nextEntry;
            }
            firstEntry.nextEntry = undefined;
            this.lastEntry = firstEntry;
        };
        class_1.prototype.keys = function () {
            return new MapIterator(this.firstEntry, function (key) { return key; });
        };
        class_1.prototype.values = function () {
            return new MapIterator(this.firstEntry, function (_key, value) { return value; });
        };
        class_1.prototype.entries = function () {
            return new MapIterator(this.firstEntry, function (key, value) { return [key, value]; });
        };
        class_1.prototype.forEach = function (action) {
            var iterator = this.entries();
            while (true) {
                var iterResult = iterator.next();
                if (iterResult.done) {
                    break;
                }
                var _a = iterResult.value, key = _a[0], value = _a[1];
                action(value, key);
            }
        };
        return class_1;
    }());
}

module.exports = shimMap();
