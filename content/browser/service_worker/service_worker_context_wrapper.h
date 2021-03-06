// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_SERVICE_WORKER_SERVICE_WORKER_CONTEXT_WRAPPER_H_
#define CONTENT_BROWSER_SERVICE_WORKER_SERVICE_WORKER_CONTEXT_WRAPPER_H_

#include <vector>

#include "base/files/file_path.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_ptr.h"
#include "content/browser/service_worker/service_worker_context_core.h"
#include "content/common/content_export.h"
#include "content/public/browser/service_worker_context.h"

namespace base {
class FilePath;
class MessageLoopProxy;
class SequencedTaskRunner;
}

namespace net {
class URLRequestContextGetter;
}

namespace quota {
class QuotaManagerProxy;
}

namespace content {

class BrowserContext;
class ChromeBlobStorageContext;
class ServiceWorkerContextCore;
class ServiceWorkerContextObserver;

// A refcounted wrapper class for our core object. Higher level content lib
// classes keep references to this class on mutliple threads. The inner core
// instance is strictly single threaded and is not refcounted, the core object
// is what is used internally in the service worker lib.
class CONTENT_EXPORT ServiceWorkerContextWrapper
    : NON_EXPORTED_BASE(public ServiceWorkerContext),
      public base::RefCountedThreadSafe<ServiceWorkerContextWrapper> {
 public:
  ServiceWorkerContextWrapper(BrowserContext* browser_context);

  // Init and Shutdown are for use on the UI thread when the profile,
  // storagepartition is being setup and torn down.
  void Init(const base::FilePath& user_data_directory,
            quota::QuotaManagerProxy* quota_manager_proxy);
  void Shutdown();

  // Deletes all files on disk and restarts the system asynchronously. This
  // leaves the system in a disabled state until it's done. This should be
  // called on the IO thread.
  void DeleteAndStartOver();

  // The core context is only for use on the IO thread.
  ServiceWorkerContextCore* context();

  // The process manager can be used on either UI or IO.
  ServiceWorkerProcessManager* process_manager() {
    return process_manager_.get();
  }

  // ServiceWorkerContext implementation:
  virtual void RegisterServiceWorker(
      const GURL& pattern,
      const GURL& script_url,
      const ResultCallback& continuation) OVERRIDE;
  virtual void UnregisterServiceWorker(const GURL& pattern,
                                       const ResultCallback& continuation)
      OVERRIDE;
  virtual void Terminate() OVERRIDE;
  virtual void GetAllOriginsInfo(const GetUsageInfoCallback& callback) OVERRIDE;
  virtual void DeleteForOrigin(const GURL& origin_url) OVERRIDE;

  void AddObserver(ServiceWorkerContextObserver* observer);
  void RemoveObserver(ServiceWorkerContextObserver* observer);

  bool is_incognito() const { return is_incognito_; }

  // The URLRequestContext doesn't exist until after the StoragePartition is
  // made (which is after this object is made). This function must be called
  // after this object is created but before any ServiceWorkerCache operations.
  // It must be called on the IO thread. If either parameter is NULL the
  // function immediately returns without forwarding to the
  // ServiceWorkerCacheStorageManager.
  void SetBlobParametersForCache(
      net::URLRequestContextGetter* request_context,
      ChromeBlobStorageContext* blob_storage_context);

 private:
  friend class base::RefCountedThreadSafe<ServiceWorkerContextWrapper>;
  friend class EmbeddedWorkerTestHelper;
  friend class ServiceWorkerProcessManager;
  virtual ~ServiceWorkerContextWrapper();

  void InitInternal(const base::FilePath& user_data_directory,
                    base::SequencedTaskRunner* stores_task_runner,
                    base::SequencedTaskRunner* database_task_runner,
                    base::MessageLoopProxy* disk_cache_thread,
                    quota::QuotaManagerProxy* quota_manager_proxy);
  void ShutdownOnIO();

  void DidDeleteAndStartOver(ServiceWorkerStatusCode status);

  void DidGetAllRegistrationsForGetAllOrigins(
      const GetUsageInfoCallback& callback,
      const std::vector<ServiceWorkerRegistrationInfo>& registrations);
  void DidGetAllRegistrationsForDeleteForOrigin(
      const GURL& origin,
      const std::vector<ServiceWorkerRegistrationInfo>& registrations);

  const scoped_refptr<ObserverListThreadSafe<ServiceWorkerContextObserver> >
      observer_list_;
  const scoped_ptr<ServiceWorkerProcessManager> process_manager_;
  // Cleared in Shutdown():
  scoped_ptr<ServiceWorkerContextCore> context_core_;

  // Initialized in Init(); true of the user data directory is empty.
  bool is_incognito_;
};

}  // namespace content

#endif  // CONTENT_BROWSER_SERVICE_WORKER_SERVICE_WORKER_CONTEXT_WRAPPER_H_
