import { useState, useRef, useEffect } from 'react';
import { Camera, Square, Play, RefreshCw, Upload } from 'lucide-react';
import { Button } from '@/app/components/ui/button';

interface CameraCaptureProps {
  onCapture: (imageData: string) => void;
  currentPhoto?: string;
}

export function CameraCapture({ onCapture, currentPhoto }: CameraCaptureProps) {
  const [isStreaming, setIsStreaming] = useState(false);
  const [isLoading, setIsLoading] = useState(false);
  const [error, setError] = useState<string>('');
  const videoRef = useRef<HTMLVideoElement>(null);
  const streamRef = useRef<MediaStream | null>(null);
  const fileInputRef = useRef<HTMLInputElement>(null);

  const startCamera = async () => {
    try {
      setError('');
      setIsLoading(true);
      
      console.log('[Camera] Starting camera...');
      console.log('[Camera] mediaDevices available:', !!navigator.mediaDevices);
      console.log('[Camera] getUserMedia available:', !!(navigator.mediaDevices && navigator.mediaDevices.getUserMedia));
      
      // First check if mediaDevices is available
      if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
        throw new Error('Camera API không khả dụng. Vui lòng đảm bảo bạn đang dùng HTTPS hoặc localhost.');
      }

      console.log('[Camera] Requesting camera permission...');
      
      // Request camera permission with fallback options
      const stream = await navigator.mediaDevices.getUserMedia({ 
        video: { 
          width: { ideal: 640 },
          height: { ideal: 480 },
          facingMode: 'user' // Front camera for patient photos
        },
        audio: false
      });
      
      console.log('[Camera] Got stream:', stream);
      
      if (videoRef.current) {
        videoRef.current.srcObject = stream;
        streamRef.current = stream;
        
        // Wait for video to be ready
        videoRef.current.onloadedmetadata = () => {
          console.log('[Camera] Video metadata loaded, playing...');
          videoRef.current?.play().then(() => {
            console.log('[Camera] Video playing');
            setIsStreaming(true);
            setIsLoading(false);
          }).catch(playErr => {
            console.error('[Camera] Play error:', playErr);
            setError('❌ Cannot play camera video.');
            setIsLoading(false);
          });
        };
        
        videoRef.current.onerror = (e) => {
          console.error('[Camera] Video error:', e);
          setError('❌ Camera video error.');
          setIsLoading(false);
        };
      }
    } catch (err: unknown) {
      setIsLoading(false);
      const error = err as Error & { name?: string };
      console.error('[Camera] Error:', error.name, error.message);
      
      if (error.name === 'NotAllowedError' || error.name === 'PermissionDeniedError') {
        setError('🚫 Camera access denied. Please allow camera access in browser settings.');
      } else if (error.name === 'NotFoundError' || error.name === 'DevicesNotFoundError') {
        setError('📷 No camera found. Please connect a webcam and try again.');
      } else if (error.name === 'NotReadableError' || error.name === 'TrackStartError') {
        setError('⚠️ Camera is being used by another application. Please close other apps and try again.');
      } else if (error.name === 'OverconstrainedError') {
        // Retry with basic constraints
        console.log('[Camera] Retrying with basic constraints...');
        try {
          const basicStream = await navigator.mediaDevices.getUserMedia({ video: true });
          if (videoRef.current) {
            videoRef.current.srcObject = basicStream;
            streamRef.current = basicStream;
            videoRef.current.onloadedmetadata = () => {
              videoRef.current?.play();
              setIsStreaming(true);
              setIsLoading(false);
              setError('');
            };
          }
        } catch {
          setError('❌ Cannot start camera.');
        }
      } else {
        setError(`❌ Camera error: ${error.message || 'Unknown'}`);
      }
    }
  };
  
  // Handle file upload as fallback
  const handleFileUpload = (event: React.ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0];
    if (file) {
      const reader = new FileReader();
      reader.onloadend = () => {
        const imageData = reader.result as string;
        onCapture(imageData);
      };
      reader.readAsDataURL(file);
    }
  };

  const stopCamera = () => {
    if (streamRef.current) {
      streamRef.current.getTracks().forEach(track => track.stop());
      streamRef.current = null;
    }
    if (videoRef.current) {
      videoRef.current.srcObject = null;
    }
    setIsStreaming(false);
  };

  const capturePhoto = () => {
    if (videoRef.current) {
      const canvas = document.createElement('canvas');
      canvas.width = videoRef.current.videoWidth;
      canvas.height = videoRef.current.videoHeight;
      const ctx = canvas.getContext('2d');
      
      if (ctx) {
        ctx.drawImage(videoRef.current, 0, 0);
        const imageData = canvas.toDataURL('image/jpeg', 0.8);
        onCapture(imageData);
        stopCamera();
      }
    }
  };

  useEffect(() => {
    return () => {
      stopCamera();
    };
  }, []);

  return (
    <div className="space-y-4">
      <div className="relative w-full aspect-video bg-gray-100 rounded-lg overflow-hidden flex items-center justify-center">
        {isStreaming ? (
          <video
            ref={videoRef}
            autoPlay
            playsInline
            className="w-full h-full object-cover"
          />
        ) : currentPhoto ? (
          <img
            src={currentPhoto}
            alt="Patient"
            className="w-full h-full object-cover"
          />
        ) : (
          <div className="text-gray-400 text-center">
            <Camera className="w-16 h-16 mx-auto mb-2" />
            <p>No photo</p>
          </div>
        )}
        
        {/* Loading overlay */}
        {isLoading && (
          <div className="absolute inset-0 bg-black/50 flex items-center justify-center">
            <div className="text-white text-center">
              <RefreshCw className="w-8 h-8 mx-auto mb-2 animate-spin" />
              <p>Starting camera...</p>
            </div>
          </div>
        )}
      </div>

      {error && (
        <div className="text-sm p-3 bg-red-50 rounded-md border border-red-200">
          <p className="text-red-600">{error}</p>
          <button 
            type="button"
            onClick={startCamera}
            className="mt-2 text-blue-600 hover:underline text-xs"
          >
            Try again
          </button>
        </div>
      )}

      <div className="flex gap-2">
        {!isStreaming ? (
          <>
            <Button
              type="button"
              onClick={startCamera}
              variant="outline"
              className="flex-1"
              disabled={isLoading}
            >
              {isLoading ? (
                <RefreshCw className="w-4 h-4 mr-2 animate-spin" />
              ) : (
                <Play className="w-4 h-4 mr-2" />
              )}
              {currentPhoto ? 'Retake' : 'Start Camera'}
            </Button>
            
            {/* File upload fallback */}
            <input
              ref={fileInputRef}
              type="file"
              accept="image/*"
              onChange={handleFileUpload}
              className="hidden"
            />
            <Button
              type="button"
              variant="outline"
              onClick={() => fileInputRef.current?.click()}
              className="flex-1"
            >
              <Upload className="w-4 h-4 mr-2" />
              Upload Photo
            </Button>
          </>
        ) : (
          <>
            <Button
              type="button"
              onClick={capturePhoto}
              className="flex-1 bg-green-600 hover:bg-green-700"
            >
              <Camera className="w-4 h-4 mr-2" />
              Capture
            </Button>
            <Button
              type="button"
              onClick={stopCamera}
              variant="destructive"
              className="flex-1"
            >
              <Square className="w-4 h-4 mr-2" />
              Stop Camera
            </Button>
          </>
        )}
      </div>

      <p className="text-xs text-gray-500">
        💡 Camera requires HTTPS or localhost. Or use "Upload Photo" to select from computer.
      </p>
    </div>
  );
}
